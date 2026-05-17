#include "ModeLineFollow.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"

// Width of the green-detection window on each side of the line
static const uint8_t GREEN_WINDOW  = 25;
static const uint8_t LINE_HALF_W   = 4;    // half-width of the black line
static const uint8_t GREEN_GAP     = 0;    // gap between line edge and green window

void modeLineFollowRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    // ── 1. Scan the line-follow row ───────────────────────────────────────────
    cameraData pixels[160] = {};
    scanRow(fb, SCAN_ROW, SCAN_COL_MIN, SCAN_COL_MAX, pixels);

    // ── 2. Count colours along the scan row ───────────────────────────────────
    int32_t weightedSum = 0;
    uint8_t blackCount  = 0;
    uint8_t silverCount = 0;
    uint8_t redCount    = 0;

    for (uint8_t c = SCAN_COL_MIN; c <= SCAN_COL_MAX; c++) {
        if (isBlack(pixels[c]))       { weightedSum += c; blackCount++; }
        if (isSilver(pixels[c]))      { silverCount++; }
        else if (isRed(pixels[c]))    { redCount++; }
    }

    // ── 3. Line centre-of-mass ────────────────────────────────────────────────
    float centerOfMass = (blackCount > 0)
        ? (float)weightedSum / blackCount
        : (SCAN_COL_MIN + SCAN_COL_MAX) / 2.0f;

    // ── 4. Green-detection windows (flanking the line) ────────────────────────
    int16_t comInt = (int16_t)centerOfMass;

    int16_t leftEndS    = comInt - LINE_HALF_W - GREEN_GAP;
    int16_t rightStartS = comInt + LINE_HALF_W + GREEN_GAP;

    uint8_t leftEnd    = (leftEndS    > 0)   ? (uint8_t)leftEndS    : 0;
    uint8_t leftStart  = (leftEnd > GREEN_WINDOW) ? leftEnd - GREEN_WINDOW : 0;
    uint8_t rightStart = (rightStartS < 159) ? (uint8_t)rightStartS : 159;
    uint8_t rightEnd   = (rightStart + GREEN_WINDOW > 159) ? 159 : (uint8_t)(rightStart + GREEN_WINDOW);

    uint8_t greenLeft = 0, greenRight = 0;

    for (uint8_t c = leftStart; c < leftEnd; c++) {
        if (c >= SCAN_COL_MIN && c <= SCAN_COL_MAX && isGreen(pixels[c])) greenLeft++;
    }
    for (uint8_t c = rightStart; c < rightEnd; c++) {
        if (c >= SCAN_COL_MIN && c <= SCAN_COL_MAX && isGreen(pixels[c])) greenRight++;
    }

    // ── 5. Feature priority: red/silver override navigation features ──────────
    uint8_t featureId = FEAT_NONE;

    if      (greenLeft > LF_GREEN_PixCOUNT_THRESHOLD && greenRight > LF_GREEN_PixCOUNT_THRESHOLD) featureId = FEAT_UTURN;
    else if (greenLeft  > LF_GREEN_PixCOUNT_THRESHOLD)                                         featureId = FEAT_GREEN_LEFT;
    else if (greenRight > LF_GREEN_PixCOUNT_THRESHOLD)                                         featureId = FEAT_GREEN_RIGHT;
    else if (blackCount > LF_BLACK_PixCOUNT_THRESHOLD)                                      featureId = FEAT_BLACK_INTERSECT;

    // No black at all → "no line"; overrides phantom green/u-turn from noise scanning around COM=midpoint.
    // Red/silver still win below.
    if (blackCount <= 5)                            featureId = FEAT_NO_LINE;

    if (redCount    > LF_RED_PixCOUNT_THRESHOLD)    featureId = FEAT_RED;
    if (silverCount > LF_SILVER_PixCOUNT_THRESHOLD) featureId = FEAT_SILVER;

    // ── 6. Send to Teensy ─────────────────────────────────────────────────────
    uint8_t scaledCOM = (uint8_t)constrain(
        map((long)(centerOfMass * 10), (long)(SCAN_COL_MIN * 10), (long)(SCAN_COL_MAX * 10), 0, 254), 0, 254);

    teensy.send(XIAO_REG_FEATURE, featureId);
    teensy.send(XIAO_REG_COM,     scaledCOM);

    // ── 7. Debug output ───────────────────────────────────────────────────────
    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=0 feat=%d com=%.1f blk=%d sil=%d red=%d gL=%d gR=%d",
        featureId, centerOfMass, blackCount, silverCount, redCount, greenLeft, greenRight);
}
