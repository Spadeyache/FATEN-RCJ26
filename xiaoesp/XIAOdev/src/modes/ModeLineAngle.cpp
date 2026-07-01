#include "ModeLineAngle.h"
#include "../processing/vision.h"
#include "../processing/LineCount.h"
#include "../config/config.h"
#include "../config/serial_print.h"
#include "../stream/XiaoStream.h"
#include <math.h>

// =============================================================================
//  Mode 3 — Line Angle config
//
//  Uses the shared border point detection (lc_detectCrossings). No in/out
//  classification: it just reduces the raw crossings to a base->tip slope.
// =============================================================================

// Angle encoding centre (0° = straight)
static constexpr uint8_t LA_ANGLE_CENTER   = 127;

// Single-point fallback: inner-circle search for a second point.
static constexpr uint8_t LA_CIRCLE_RADIUS  = 25;   // px
static constexpr uint8_t LA_CIRCLE_STEPS   = 96;   // samples around the circle
static constexpr uint8_t LA_CIRCLE_MIN_RUN = 2;    // min contiguous black samples to qualify
static constexpr uint8_t LA_FINE_ROW_FAR   = 35;   // fixed rows for precise final alignment
static constexpr uint8_t LA_FINE_ROW_NEAR  = 70;
static constexpr uint8_t LA_FINE_MIN_RUN   = LC_RUN_MIN_LEN;
static constexpr uint8_t LA_RED_ROW        = 65;
static constexpr uint8_t LA_RED_THRESHOLD  = 8;
static constexpr uint8_t LA_RED_FRAMES     = 3;

static constexpr float   LA_RAD2DEG        = 57.2957795f;

static uint8_t s_redFrames = 0;

static bool hasBottomLinePoint(const LineCounts& lc) {
    for (uint8_t i = 0; i < lc.count; i++) {
        if (lc.crossings[i].edge == LC_EDGE_BOTTOM) return true;
    }
    return false;
}

// =============================================================================
//  Pick the two crossings with the largest width.
//  Fills aOut/bOut (no particular order). Requires lc.count >= 2.
// =============================================================================
static void twoWidest(const LineCounts& lc, Crossing& aOut, Crossing& bOut) {
    int ia = -1, ib = -1;
    for (int i = 0; i < lc.count; i++) {
        if (ia < 0 || lc.crossings[i].width > lc.crossings[ia].width) {
            ib = ia;
            ia = i;
        } else if (ib < 0 || lc.crossings[i].width > lc.crossings[ib].width) {
            ib = i;
        }
    }
    aOut = lc.crossings[ia];
    bOut = lc.crossings[ib];
}

// =============================================================================
//  Inner-circle continuous point detection.
//  Walks a circle of radius `radius` around (cx, cy), run-counts contiguous
//  black samples, and returns the midpoint of the largest qualifying run.
//  Returns true when a second point was found.
// =============================================================================
static inline bool inLineAngleSearchBox(int x, int y) {
    return x >= LC_ROI_X_MIN && x <= LC_ROI_X_MAX &&
           y >= LC_ROI_Y_TOP && y <= LC_ROI_Y_BOT;
}

static bool scanCircleForPoint(camera_fb_t* fb, const Crossing& seed,
                               uint8_t radius, Crossing& qOut) {
    bool black[LA_CIRCLE_STEPS];
    uint8_t sx[LA_CIRCLE_STEPS], sy[LA_CIRCLE_STEPS];

    for (uint8_t i = 0; i < LA_CIRCLE_STEPS; i++) {
        const float a = (2.0f * (float)M_PI * (float)i) / (float)LA_CIRCLE_STEPS;
        const int x = (int)lroundf((float)seed.pixelX + (float)radius * cosf(a));
        const int y = (int)lroundf((float)seed.pixelY + (float)radius * sinf(a));
        sx[i] = (uint8_t)constrain(x, 0, (int)fb->width - 1);
        sy[i] = (uint8_t)constrain(y, 0, (int)fb->height - 1);
        const bool inFrame = (x >= 0 && x < (int)fb->width && y >= 0 && y < (int)fb->height);
        black[i] = inFrame && inLineAngleSearchBox(x, y) &&
                   isBlack(updateRawGrayHSV(fb, sx[i], sy[i]));
    }

    // Largest circular black run.
    int bestStart = -1, bestLen = 0;
    int k = 0;
    // Rotate so we start on a white sample (so no run wraps the array end).
    int origin = 0;
    while (origin < LA_CIRCLE_STEPS && black[origin]) origin++;
    if (origin == LA_CIRCLE_STEPS) origin = 0;   // whole circle black → treat [0..] as one run

    while (k < LA_CIRCLE_STEPS) {
        const int idx = (origin + k) % LA_CIRCLE_STEPS;
        if (!black[idx]) { k++; continue; }
        const int runStartK = k;
        int len = 0;
        while (k < LA_CIRCLE_STEPS && black[(origin + k) % LA_CIRCLE_STEPS]) { len++; k++; }
        if (len > bestLen) { bestLen = len; bestStart = runStartK; }
    }

    if (bestLen < LA_CIRCLE_MIN_RUN) return false;

    const int midIdx = (origin + bestStart + bestLen / 2) % LA_CIRCLE_STEPS;
    qOut.pixelX = sx[midIdx];
    qOut.pixelY = sy[midIdx];
    qOut.edge   = LC_EDGE_TOP;
    qOut.pos    = 0.0f;
    qOut.width  = (uint8_t)bestLen;
    return true;
}

static bool findRowRunCenter(camera_fb_t* fb, uint8_t y, uint8_t& xOut, uint8_t& widthOut) {
    int bestStart = -1, bestLen = 0;
    int len = 0, start = LC_ROI_X_MIN;

    for (int x = LC_ROI_X_MIN; x <= LC_ROI_X_MAX; x++) {
        const bool black = isBlack(updateRawGrayHSV(fb, (uint8_t)x, y));
        if (black) {
            if (len == 0) start = x;
            len++;
        } else if (len > 0) {
            if (len > bestLen) { bestLen = len; bestStart = start; }
            len = 0;
        }
    }
    if (len > bestLen) { bestLen = len; bestStart = start; }

    if (bestLen < LA_FINE_MIN_RUN || bestStart < 0) return false;
    xOut = (uint8_t)(bestStart + bestLen / 2);
    widthOut = (uint8_t)(bestLen > 255 ? 255 : bestLen);
    return true;
}

static bool scanRedRow(camera_fb_t* fb) {
    uint8_t redCount = 0;
    for (uint8_t x = LC_ROI_X_MIN; x <= LC_ROI_X_MAX; x++) {
        if (isRed(updateRawGrayHSV(fb, x, LA_RED_ROW))) redCount++;
    }
    return redCount >= LA_RED_THRESHOLD;
}

static bool twoRowFineAngle(camera_fb_t* fb, float& angleOut,
                            uint8_t& farXOut, uint8_t& nearXOut,
                            uint8_t& farWOut, uint8_t& nearWOut) {
    if (LA_FINE_ROW_FAR < LC_ROI_Y_TOP || LA_FINE_ROW_FAR > LC_ROI_Y_BOT) return false;
    if (LA_FINE_ROW_NEAR < LC_ROI_Y_TOP || LA_FINE_ROW_NEAR > LC_ROI_Y_BOT) return false;

    if (!findRowRunCenter(fb, LA_FINE_ROW_FAR, farXOut, farWOut)) return false;
    if (!findRowRunCenter(fb, LA_FINE_ROW_NEAR, nearXOut, nearWOut)) return false;

    const float dx = (float)nearXOut - (float)farXOut;
    const float dy = (float)LA_FINE_ROW_NEAR - (float)LA_FINE_ROW_FAR;
    angleOut = atan2f(dx, dy) * LA_RAD2DEG;
    return true;
}

// =============================================================================
//  modeLineAngleRun — called every frame while in MODE_LINE_ANGLE
// =============================================================================
void modeLineAngleReset() {
    s_redFrames = 0;
}

void modeLineAngleRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    // ── Point detection (no in/out classification) ─────────────────────────────
    LineCounts lc;
    lc_detectCrossings(fb, lc);

    const bool haveAny = (lc.count >= 1);
    const bool bottomLinePoint = hasBottomLinePoint(lc);

    // ── Reduce to a base + tip pair ────────────────────────────────────────────
    Crossing base = {}, tip = {};
    bool haveTwoForAngle = false;
    const bool haveTwoDetected = (lc.count >= 2);

    if (lc.count >= 2) {
        Crossing a, b;
        twoWidest(lc, a, b);
        // Base = lower in the frame (larger pixelY = nearer the robot / lower power).
        if (a.pixelY >= b.pixelY) { base = a; tip = b; }
        else                      { base = b; tip = a; }
        haveTwoForAngle = true;
    } else if (lc.count == 1) {
        const Crossing p = lc.crossings[0];
        Crossing q;
        if (scanCircleForPoint(fb, p, LA_CIRCLE_RADIUS, q)) {
            if (p.pixelY >= q.pixelY) { base = p; tip = q; }
            else                      { base = q; tip = p; }
            haveTwoForAngle = true;
        } else {
            base = p;   // single point only — no slope available
        }
    }

    // ── Slope angle (base -> tip) ──────────────────────────────────────────────
    // dx/dy keep the existing mode-3 sign convention (base.x - tip.x over the
    // positive vertical span), so the Teensy side is unchanged.
    float angleDeg = 0.0f;
    uint8_t avgY = 0;
    if (haveTwoForAngle) {
        const float dx = (float)base.pixelX - (float)tip.pixelX;
        float dy = (float)base.pixelY - (float)tip.pixelY;
        if (dy < 1.0f) dy = 1.0f;
        angleDeg = atan2f(dx, dy) * LA_RAD2DEG;
        avgY = haveTwoDetected ? (uint8_t)(((uint16_t)base.pixelY + (uint16_t)tip.pixelY) / 2)
                               : base.pixelY;
    } else if (haveAny) {
        avgY = base.pixelY;
    }

    const uint8_t encodedAngle = (uint8_t)constrain(
        (int)roundf((float)LA_ANGLE_CENTER + angleDeg), 0, 254);
    const uint8_t avgYByte = (uint8_t)(avgY > 254 ? 254 : avgY);

    float fineAngleDeg = 0.0f;
    uint8_t fineFarX = 0, fineNearX = 0, fineFarW = 0, fineNearW = 0;
    const bool fineValid = twoRowFineAngle(fb, fineAngleDeg, fineFarX, fineNearX, fineFarW, fineNearW);
    const uint8_t encodedFineAngle = (uint8_t)constrain(
        (int)roundf((float)LA_ANGLE_CENTER + fineAngleDeg), 0, 254);

    // ── Flags: bit0 = at least one point, bit1 = two or more points, bit2 = bottom point
    if (scanRedRow(fb)) {
        if (s_redFrames < LA_RED_FRAMES) s_redFrames++;
    } else {
        s_redFrames = 0;
    }
    const bool redConfirmed = s_redFrames >= LA_RED_FRAMES;

    uint8_t flag = 0;
    if (haveAny) flag |= XIAO_FLAG_COMMIT;
    if (haveTwoDetected) flag |= XIAO_FLAG_TIGHT_SLOW;
    if (bottomLinePoint) flag |= XIAO_FLAG_BOTTOM_LINE;
    if (fineValid) flag |= XIAO_FLAG_FINE_ANGLE;

    // ── Transmit ───────────────────────────────────────────────────────────────
    teensy.send(XIAO_REG_ANGLE, encodedAngle);
    teensy.send(XIAO_REG_FLAG,  flag);
    teensy.send(XIAO_REG_COM,   avgYByte);
    teensy.send(XIAO_REG_FINE_ANGLE, encodedFineAngle);
    teensy.send(XIAO_REG_FEATURE, redConfirmed ? FEAT_RED : FEAT_NONE);

    LineClass dbgCls = {};
    dbgCls.inIndex = -1;
    dbgCls.outCount = lc.count;
    dbgCls.inHeld = false;
    dbgCls.inPos = 0.0f;
    xs_storeLineDebug(lc, dbgCls, -1, encodedAngle, angleDeg);

    xs_storeLineAngleDebug(
        lc.count,
        haveTwoDetected,
        bottomLinePoint,
        haveTwoForAngle && !haveTwoDetected,
        haveAny ? base.pixelX : -1,
        haveAny ? base.pixelY : -1,
        haveAny ? base.width : 0,
        haveTwoForAngle ? tip.pixelX : -1,
        haveTwoForAngle ? tip.pixelY : -1,
        haveTwoForAngle ? tip.width : 0,
        angleDeg,
        encodedAngle,
        avgYByte,
        flag,
        fineValid,
        fineAngleDeg,
        encodedFineAngle,
        fineValid ? fineFarX : -1,
        LA_FINE_ROW_FAR,
        fineFarW,
        fineValid ? fineNearX : -1,
        LA_FINE_ROW_NEAR,
        fineNearW);

    SPRINTF(SPRINT_RESULTS, "[LA]",
        "mode=3 n=%d two=%d bottom=%d circ=%d base=(%d,%d) bw=%d tip=(%d,%d) tw=%d ang=%.1f enc=%d fine=%d fang=%.1f fenc=%d far=(%d,%d) fw=%d near=(%d,%d) nw=%d avgY=%d flag=%d",
        lc.count, haveTwoDetected ? 1 : 0, bottomLinePoint ? 1 : 0, (haveTwoForAngle && !haveTwoDetected) ? 1 : 0,
        haveAny ? base.pixelX : -1, haveAny ? base.pixelY : -1,
        haveAny ? base.width : 0,
        haveTwoForAngle ? tip.pixelX : -1,  haveTwoForAngle ? tip.pixelY : -1,
        haveTwoForAngle ? tip.width : 0,
        angleDeg, encodedAngle,
        fineValid ? 1 : 0, fineAngleDeg, encodedFineAngle,
        fineValid ? fineFarX : -1, LA_FINE_ROW_FAR, fineFarW,
        fineValid ? fineNearX : -1, LA_FINE_ROW_NEAR, fineNearW,
        avgYByte, flag ? 1 : 0);
}
