#include "ModeLineFollow2.h"
#include "LineCount.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"

#include <Arduino.h>

// =============================================================================
//  modeLineFollow2 — clean two/three-row line follow that drives CONTINUOUSLY
//  through intersections (see ModeLineFollow2.h).
//
//  Per frame:
//    base : row NEAR (70) black center-of-mass → error (0..254, 127 = centre);
//           too little black → no line (error = 127, FEAT_LINE_LOST).
//    edge : row FAR (40) red → FEAT_RED; row FAR black-saturated → light the LED.
//    on saturation (a bar ahead), decide how to cross:
//      no straight line at row TOP (5)  → 2-point in/out follow (no commit/flag)
//      line continues + both green      → FEAT_UTURN (Teensy spins; no flag)
//      line continues + left/right green → committed 2-point turn + FLAG raised
//      line continues + no green        → 3-point straight-through follow
//
//  Latches: a committed green turn (lc_commit*) runs until 1-in/1-out & straight
//  (< LF2_STRAIGHT_DEG); the 3-point follow runs until the NEAR row saturates.
//  Everything else is decided fresh each frame.
// =============================================================================

namespace {

constexpr uint8_t GREEN_WINDOW = 25;   // green search width on each side of the line
constexpr uint8_t LINE_HALF_W  = 4;    // half width of the black line (skip past it)

// Shared row buffer for the scan helpers. They run only on Core 1 (the vision
// loop) and never overlap, so a single static buffer is safe — and keeps these
// ~1.3 KB arrays OFF the loop-task stack (several per frame would overflow it).
cameraData s_rowPx[160];

// Map a CoM column [SCAN_COL_MIN..MAX] → error byte 0..254 (centre column → 127).
uint8_t comToError(float com) {
    return (uint8_t)constrain(
        map((long)(com * 10), (long)(SCAN_COL_MIN * 10), (long)(SCAN_COL_MAX * 10), 0, 254),
        0, 254);
}

// Scan one row: black pixel count + center-of-mass column.
void scanBlackRow(camera_fb_t* fb, uint8_t row, float& com, uint8_t& black) {
    cameraData* px = s_rowPx;
    scanRow(fb, row, SCAN_COL_MIN, SCAN_COL_MAX, px);
    int32_t weighted = 0;
    black = 0;
    for (uint8_t c = SCAN_COL_MIN; c <= SCAN_COL_MAX; c++) {
        if (isBlack(px[c])) { weighted += c; black++; }
    }
    com = black ? (float)weighted / black : (SCAN_COL_MIN + SCAN_COL_MAX) / 2.0f;
}

// Scan one row for the edge-case colors: black + red counts.
void scanRedBlack(camera_fb_t* fb, uint8_t row, uint8_t& black, uint8_t& red) {
    cameraData* px = s_rowPx;
    scanRow(fb, row, SCAN_COL_MIN, SCAN_COL_MAX, px);
    black = 0; red = 0;
    for (uint8_t c = SCAN_COL_MIN; c <= SCAN_COL_MAX; c++) {
        if (isBlack(px[c])) black++;
        if (isRed(px[c]))   red++;
    }
}

// Green pixel counts in the windows flanking the line CoM at the given row.
void greenAtRow(camera_fb_t* fb, uint8_t row, float comCol, uint8_t& gL, uint8_t& gR) {
    cameraData* px = s_rowPx;
    scanRow(fb, row, SCAN_COL_MIN, SCAN_COL_MAX, px);

    const int16_t comI = (int16_t)comCol;
    uint8_t leftEnd    = (comI - LINE_HALF_W > 0) ? (uint8_t)(comI - LINE_HALF_W) : 0;
    uint8_t leftStart  = (leftEnd > GREEN_WINDOW) ? (uint8_t)(leftEnd - GREEN_WINDOW) : 0;
    uint8_t rightStart = (comI + LINE_HALF_W < 159) ? (uint8_t)(comI + LINE_HALF_W) : 159;
    uint8_t rightEnd   = (rightStart + GREEN_WINDOW > 159) ? 159 : (uint8_t)(rightStart + GREEN_WINDOW);

    gL = 0; gR = 0;
    for (uint8_t c = leftStart; c < leftEnd; c++)
        if (c >= SCAN_COL_MIN && c <= SCAN_COL_MAX && isGreen(px[c])) gL++;
    for (uint8_t c = rightStart; c < rightEnd; c++)
        if (c >= SCAN_COL_MIN && c <= SCAN_COL_MAX && isGreen(px[c])) gR++;
}

// 3-point straight-through error: scan rows TOP/FAR/NEAR, drop the most-black row
// (the perpendicular bar), fit the other two CoMs and project to the NEAR row.
uint8_t threePointError(camera_fb_t* fb, uint8_t fallback) {
    const uint8_t rows[3] = { LF2_ROW_TOP, LF2_ROW_FAR, LF2_ROW_NEAR };
    float   com[3];
    uint8_t blk[3];
    for (int i = 0; i < 3; i++) scanBlackRow(fb, rows[i], com[i], blk[i]);

    int drop = 0;                                   // row with the most black = the bar
    for (int i = 1; i < 3; i++) if (blk[i] > blk[drop]) drop = i;
    int a = -1, b = -1;
    for (int i = 0; i < 3; i++) if (i != drop) { (a < 0 ? a : b) = i; }

    if (blk[a] < LF2_NOLINE_BLACK_MIN || blk[b] < LF2_NOLINE_BLACK_MIN) return fallback;

    const float slope = (com[b] - com[a]) / ((float)rows[b] - (float)rows[a]);
    const float proj  = com[a] + slope * ((float)LF2_ROW_NEAR - (float)rows[a]);
    return comToError(proj);
}

// Committed 2-point follow error toward the locked green-side branch.
uint8_t committedError(const LineCounts& lc, const LineClass& cls, int& outUsed, uint8_t fallback) {
    int outIdx = -1;
    lc_commitUpdate(lc, cls.inIndex, cls.outCount, outIdx);
    outUsed = (outIdx >= 0) ? outIdx : lc_focusedOut(lc, cls.inIndex);
    float e;
    if (lc_slopeError(lc, cls.inIndex, outUsed, e)) return (uint8_t)constrain((int)(e + 0.5f), 0, 254);
    return fallback;
}

}  // namespace

void modeLineFollow2Run(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    static uint8_t s_lastErr    = LF_ERROR_CENTER;   // held when a follow can't produce an error
    static bool    s_threePoint = false;             // latched 3-point straight-through

    // Crossings (LineCount box) — needed for the 2-point / committed follows.
    LineCounts lc;  lc_detectCrossings(fb, lc);
    LineClass  cls = lc_updateIn(lc);

    float   com60;  uint8_t black60;  scanBlackRow(fb, LF2_ROW_NEAR, com60, black60);
    uint8_t black40, red40;           scanRedBlack(fb, LF2_ROW_FAR, black40, red40);
    float   com5;   uint8_t black5;   scanBlackRow(fb, LF2_ROW_TOP, com5, black5);

    const bool saturated = (black40 > LF2_SATURATION_BLACK_MIN);
    // LED debug disabled for normal running.
    // digitalWrite(LED_BUILTIN, saturated ? LOW : HIGH);   // ESP32 LED active-LOW

    uint8_t feature = FEAT_NONE;
    uint8_t flag    = 0;
    uint8_t err     = s_lastErr;

    // ── A. LATCHED: committed green 2-point turn ──────────────────────────────
    if (lc_commitActive()) {
        int out = -1;
        err  = committedError(lc, cls, out, s_lastErr);
        flag = 1;
        if (cls.outCount == 1 && lc_inOutAngleDeg(lc, cls.inIndex, out) < LF2_STRAIGHT_DEG)
            lc_commitClear();                            // 1-in/1-out & straight → turn done
    }
    // ── B. LATCHED: 3-point straight-through ──────────────────────────────────
    else if (s_threePoint) {
        err = threePointError(fb, s_lastErr);
        if (black60 > LF2_SATURATION_BLACK_MIN) s_threePoint = false;   // bar reached near row
    }
    // ── C. Per-frame base + intersection entry ────────────────────────────────
    else {
        if (black60 < LF2_NOLINE_BLACK_MIN) { err = LF_ERROR_CENTER; feature = FEAT_LINE_LOST; }
        else                                  err = comToError(com60);

        if (red40 > LF_RED_PixCOUNT_THRESHOLD) feature = FEAT_RED;   // red ahead overrides

        if (saturated && feature != FEAT_RED) {
            if (black5 < LF2_STRAIGHT_BLACK_MIN) {
                // No straight continuation → 2-point in/out turn (no commit, no flag).
                int out = lc_focusedOut(lc, cls.inIndex);
                float e;
                if (lc_slopeError(lc, cls.inIndex, out, e)) err = (uint8_t)constrain((int)(e + 0.5f), 0, 254);
            } else {
                // Line continues → read green at the two rows.
                uint8_t gLa, gRa, gLb, gRb;
                greenAtRow(fb, LF2_GREEN_ROW_A, com60, gLa, gRa);
                greenAtRow(fb, LF2_GREEN_ROW_B, com60, gLb, gRb);
                const bool gL = (gLa > LF_GREEN_PixCOUNT_THRESHOLD) || (gLb > LF_GREEN_PixCOUNT_THRESHOLD);
                const bool gR = (gRa > LF_GREEN_PixCOUNT_THRESHOLD) || (gRb > LF_GREEN_PixCOUNT_THRESHOLD);

                if (gL && gR) {
                    feature = FEAT_UTURN;                              // Teensy spins; no flag
                } else if (gL || gR) {
                    lc_commitStart(gL);                                // gL → left, else right
                    flag = 1;
                    int out = -1;
                    err = committedError(lc, cls, out, err);
                } else {
                    s_threePoint = true;                               // straight, no green
                    err = threePointError(fb, err);
                }
            }
        }
    }

    s_lastErr = err;
    lc_storeDebug(lc, cls, lc_focusedOut(lc, cls.inIndex), err, (float)err - (float)LF_ERROR_CENTER);
    row55_storeDebug(
        feature == FEAT_RED ? ROW55_RED : (feature == FEAT_SILVER ? ROW55_SILVER : (feature == FEAT_LINE_LOST ? ROW55_WHITE : ROW55_BLACK)),
        feature == FEAT_RED ? ROW55_RED : (feature == FEAT_SILVER ? ROW55_SILVER : (feature == FEAT_LINE_LOST ? ROW55_WHITE : ROW55_BLACK)));
    teensy.send(XIAO_REG_FEATURE, feature);
    teensy.send(XIAO_REG_COM,     err);
    teensy.send(XIAO_REG_FLAG,    flag);

    SPRINTF(SPRINT_RESULTS, "[RES2]",
        "err=%d feat=%d flag=%d nBlk=%d fBlk=%d red=%d topBlk=%d sat=%d 3pt=%d cmt=%d",
        err, feature, flag, black60, black40, red40, black5, saturated ? 1 : 0,
        s_threePoint ? 1 : 0, lc_commitActive() ? 1 : 0);
}
