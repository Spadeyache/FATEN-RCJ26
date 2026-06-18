#include "ModeLineFollow.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"
#include "LineCount.h"

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

    // ── 6. LineCount: in/out detection → focused-out → pixel lookahead error ──
    //  Layer 1 counts border crossings, Layer 2 classifies 1 in + N out, then the
    //  focused-out is the main steering target. The in-point is blended in only
    //  as a small pixel-space stabilizer; no angle conversion is used here.
    LineCounts lc;
    lc_detectCrossings(fb, lc);
    LineClass cls = lc_updateIn(lc);
    int fo = lc_focusedOut(lc, cls.inIndex);

    static float s_lastErr = (float)LF_ERROR_CENTER;   // held across frames on loss
    static float s_lastErrPx = 0.0f;
    float pixelErr;
    float errPx;
    if (lc_slopeError(lc, cls.inIndex, fo, pixelErr, &errPx)) {
        s_lastErr = pixelErr;
        s_lastErrPx = errPx;
    }  // else hold last
    uint8_t errByte = (uint8_t)constrain((int)(s_lastErr + 0.5f), 0, 254);

    lc_storeDebug(lc, cls, fo, errByte, s_lastErrPx);

    // ── 7. Send to Teensy ─────────────────────────────────────────────────────
    //  COM register now carries the pixel-space lookahead error (held on loss).
    //  The row-55 center-of-mass is kept only for the green windows above.
    teensy.send(XIAO_REG_FEATURE, featureId);
    teensy.send(XIAO_REG_COM,     errByte);

    // ── 8. Debug output ───────────────────────────────────────────────────────
    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=0 feat=%d com=%.1f err=%d epx=%.1f blk=%d sil=%d red=%d gL=%d gR=%d",
        featureId, centerOfMass, errByte, s_lastErrPx, blackCount, silverCount, redCount, greenLeft, greenRight);
#ifndef OUTPUT_STREAM
    int xIn = (cls.inIndex >= 0 && cls.inIndex < lc.count) ? lc.crossings[cls.inIndex].pixelX : -1;
    int xOut = (fo >= 0 && fo < lc.count) ? lc.crossings[fo].pixelX : -1;
    SPRINTF(SPRINT_RESULTS, "[LC]",
        "n=%d in=%d fo=%d xi=%d xo=%d epx=%.1f err=%d held=%d out=%d",
        lc.count, cls.inIndex, fo, xIn, xOut, s_lastErrPx, errByte, cls.inHeld ? 1 : 0, cls.outCount);
#endif
}
