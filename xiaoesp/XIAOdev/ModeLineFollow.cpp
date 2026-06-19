#include "ModeLineFollow.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"
#include "LineCount.h"
#include "GreenFilter.h"

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

    // ── 5. Raw per-frame green observation (fed to the vote filter) ───────────
    uint8_t rawGreen = 0;
    if      (greenLeft > LF_GREEN_PixCOUNT_THRESHOLD && greenRight > LF_GREEN_PixCOUNT_THRESHOLD) rawGreen = 1; // U-turn
    else if (greenLeft  > LF_GREEN_PixCOUNT_THRESHOLD)                                            rawGreen = 2; // left
    else if (greenRight > LF_GREEN_PixCOUNT_THRESHOLD)                                            rawGreen = 3; // right

    // ── 6. Row-55 colour readout (display only) ───────────────────────────────
    uint8_t rowLeft = ROW55_WHITE, rowRight = ROW55_WHITE;
    if (redCount > LF_RED_PixCOUNT_THRESHOLD) {
        rowLeft = rowRight = ROW55_RED;
    } else if (silverCount > LF_SILVER_PixCOUNT_THRESHOLD) {
        rowLeft = rowRight = ROW55_SILVER;
    } else {
        const bool linePresent = (blackCount > 5);
        rowLeft  = (greenLeft  > LF_GREEN_PixCOUNT_THRESHOLD) ? ROW55_GREEN : (linePresent ? ROW55_BLACK : ROW55_WHITE);
        rowRight = (greenRight > LF_GREEN_PixCOUNT_THRESHOLD) ? ROW55_GREEN : (linePresent ? ROW55_BLACK : ROW55_WHITE);
    }

    // ── 7. LineCount: in/out detection → focused-out ──────────────────────────
    LineCounts lc;
    lc_detectCrossings(fb, lc);
    LineClass cls = lc_updateIn(lc);
    int fo = lc_focusedOut(lc, cls.inIndex);

    // ── 8. Green vote filter → committed goal ─────────────────────────────────
    //  Green left/right are handled locally now: a confirmed turn seeds a virtual
    //  committed goal that is tracked for COMMIT_MS, steering the error through the
    //  intersection. Only U-turn is reported to the Teensy (via FEATURE below).
    uint8_t greenCmd = lc_commitActive() ? 0 : gf_update(rawGreen);
    if (greenCmd == 2) { lc_commitStart(true);  gf_reset(); }   // green-left  → commit left
    if (greenCmd == 3) { lc_commitStart(false); gf_reset(); }   // green-right → commit right

    //  The commit holds until the line settles to a single continuation for
    //  COMMIT_END_FRAMES consecutive frames (handled inside lc_commitUpdate).
    int  commitIdx    = -1;
    bool commitActive = lc_commitUpdate(lc, cls.inIndex, cls.outCount, commitIdx);
    int  steerOut     = (commitActive && commitIdx >= 0) ? commitIdx : fo;

    // ── 9. Pixel-lookahead error (held on loss / committed-but-unseen) ────────
    static float s_lastErr   = (float)LF_ERROR_CENTER;
    static float s_lastErrPx = 0.0f;
    float pixelErr, errPx;
    bool  fresh = false;
    if (!(commitActive && commitIdx < 0))           // committed but goal unseen → hold last
        fresh = lc_slopeError(lc, cls.inIndex, steerOut, pixelErr, &errPx);
    if (fresh) { s_lastErr = pixelErr; s_lastErrPx = errPx; }
    uint8_t errByte = (uint8_t)constrain((int)(s_lastErr + 0.5f), 0, 254);

    // ── 10. Feature priority (green L/R migrated to the commit above) ─────────
    uint8_t featureId = FEAT_NONE;
    if (greenCmd == 1)                                                  featureId = FEAT_UTURN;
    else if (!commitActive && blackCount > LF_BLACK_PixCOUNT_THRESHOLD) featureId = FEAT_BLACK_INTERSECT;
    if (!commitActive && blackCount <= 5)           featureId = FEAT_NO_LINE;
    if (redCount    > LF_RED_PixCOUNT_THRESHOLD)    featureId = FEAT_RED;
    if (silverCount > LF_SILVER_PixCOUNT_THRESHOLD) featureId = FEAT_SILVER;

    // ── 11. Store debug + send to Teensy ──────────────────────────────────────
    lc_storeDebug(lc, cls, fo, errByte, s_lastErrPx);
    lc_storeSteer(steerOut, commitActive, lc_commitLocked(), lc_commitProgress(), greenCmd);
    row55_storeDebug(rowLeft, rowRight);

    teensy.send(XIAO_REG_FEATURE, featureId);
    teensy.send(XIAO_REG_COM,     errByte);

    // ── 12. Debug output ──────────────────────────────────────────────────────
    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=0 feat=%d com=%.1f err=%d epx=%.1f blk=%d sil=%d red=%d gL=%d gR=%d gc=%d cmt=%d",
        featureId, centerOfMass, errByte, s_lastErrPx, blackCount, silverCount, redCount,
        greenLeft, greenRight, greenCmd, commitActive ? 1 : 0);
#ifndef OUTPUT_STREAM
    int xIn  = (cls.inIndex >= 0 && cls.inIndex < lc.count) ? lc.crossings[cls.inIndex].pixelX : -1;
    int xOut = (steerOut    >= 0 && steerOut    < lc.count) ? lc.crossings[steerOut].pixelX    : -1;
    SPRINTF(SPRINT_RESULTS, "[LC]",
        "n=%d in=%d steer=%d xi=%d xo=%d err=%d act=%d lock=%d prog=%d g=%d",
        lc.count, cls.inIndex, steerOut, xIn, xOut, errByte, commitActive ? 1 : 0,
        lc_commitLocked() ? 1 : 0, lc_commitProgress(), greenCmd);
#endif
}
