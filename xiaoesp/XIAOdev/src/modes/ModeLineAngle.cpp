#include "ModeLineAngle.h"
#include "../processing/vision.h"
#include "../processing/LineCount.h"
#include "../config/config.h"
#include "../config/serial_print.h"
#include <math.h>

// =============================================================================
//  Mode 3 — Line Angle config
//
//  Two scan rows: BOTTOM (near robot) and MID (look-ahead).
//  Column range matches the line-follow arc bottom edge.
//  Black detection thresholds copied from LineCount / line-follow constants.
// =============================================================================

// Scan rows
static constexpr uint8_t LA_ROW_BOTTOM      = LF_ARC_BOTTOM_Y;   // near robot
static constexpr uint8_t LA_ROW_MID         = 40;   // look-ahead row

// Scan columns (= ARC_BOTTOM_LEFT_X .. ARC_BOTTOM_RIGHT_X in line follow)
static constexpr uint8_t LA_COL_MIN         = LF_ARC_BOTTOM_LEFT_X;
static constexpr uint8_t LA_COL_MAX         = LF_ARC_BOTTOM_RIGHT_X;

// Minimum contiguous black pixels to register as a line (= LC_RUN_MIN_LEN)
static constexpr uint8_t LA_MIN_CHUNK_PX    = 3;

// Minimum chunk width per row for the both-rows flag (10 px = ~10 mm line width)
static constexpr uint8_t LA_FLAG_MIN_PX     = 10;

// Angle encoding centre (0° = straight)
static constexpr uint8_t LA_ANGLE_CENTER    = 127;

// =============================================================================
//  Row scan — find largest contiguous black chunk; return its CoM and width.
//  Returns true when a qualifying chunk (≥ minPx) was found.
// =============================================================================
static bool scanRowChunk(camera_fb_t* fb, uint8_t row,
                         uint8_t colMin, uint8_t colMax,
                         uint8_t minPx,
                         float& comOut, uint8_t& widthOut) {
    uint8_t bestStart = colMin, bestLen = 0;
    uint8_t runStart  = colMin, runLen  = 0;

    for (uint8_t x = colMin; x <= colMax; x++) {
        uint8_t r, g, b;
        rgb565To888(unpackRGB565(fb->buf, (int)row * fb->width + x), r, g, b);
        rgb888Calibration(r, g, b);
        const bool black = (rgbToGray(r, g, b) <= BLACK_GRAY_MAX);

        if (black) {
            if (runLen == 0) runStart = x;
            runLen++;
        } else {
            if (runLen > bestLen) { bestLen = runLen; bestStart = runStart; }
            runLen = 0;
        }
    }
    if (runLen > bestLen) { bestLen = runLen; bestStart = runStart; }

    if (bestLen < minPx) return false;

    // CoM = midpoint of the best run
    comOut   = bestStart + (bestLen - 1) * 0.5f;
    widthOut = bestLen;
    return true;
}

// =============================================================================
//  modeLineAngleRun — called every frame while in MODE_LINE_ANGLE
// =============================================================================
void modeLineAngleRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    // ── Row scans ─────────────────────────────────────────────────────────────
    float   botCom = (LA_COL_MIN + LA_COL_MAX) * 0.5f, midCom = botCom;
    uint8_t botWidth = 0, midWidth = 0;

    const bool botSeen = scanRowChunk(fb, LA_ROW_BOTTOM, LA_COL_MIN, LA_COL_MAX,
                                      LA_MIN_CHUNK_PX, botCom, botWidth);
    const bool midSeen = scanRowChunk(fb, LA_ROW_MID,    LA_COL_MIN, LA_COL_MAX,
                                      LA_MIN_CHUNK_PX, midCom, midWidth);

    // ── Slope angle ───────────────────────────────────────────────────────────
    // Vector from mid-row focused point → bottom-row focused point.
    // dy is always positive (bottom row has larger Y = closer to robot).
    float angleDeg = 0.0f;
    if (botSeen && midSeen) {
        const float dx = botCom - midCom;
        const float dy = (float)(LA_ROW_BOTTOM - LA_ROW_MID);  // = 45, always > 0
        angleDeg = atan2f(dx, dy) * 57.2957795f;               // -90..+90
    }
    const uint8_t encodedAngle = (uint8_t)constrain(
        (int)roundf((float)LA_ANGLE_CENTER + angleDeg), 0, 254);

    // ── Both-rows flag ────────────────────────────────────────────────────────
    const bool botQual = botWidth >= LA_FLAG_MIN_PX;
    const bool midQual = midWidth >= LA_FLAG_MIN_PX;
    const uint8_t bothFlag = (botQual && midQual) ? XIAO_FLAG_COMMIT : 0;

    // ── Arc crossing count (same ROI as line follow) ──────────────────────────
    LineCounts lc;
    lc_detectCrossings(fb, lc);
    const uint8_t crossCount = (uint8_t)(lc.count < 254 ? lc.count : 254);

    // ── Transmit ──────────────────────────────────────────────────────────────
    teensy.send(XIAO_REG_ANGLE,   encodedAngle);
    teensy.send(XIAO_REG_FLAG,    bothFlag);
    teensy.send(XIAO_REG_COM,     crossCount);

    SPRINTF(SPRINT_RESULTS, "[LA]",
        "mode=3 bot=%d(w=%d,com=%.1f) mid=%d(w=%d,com=%.1f) ang=%.1f enc=%d flag=%d cross=%d",
        botSeen ? 1 : 0, botWidth, botCom,
        midSeen ? 1 : 0, midWidth, midCom,
        angleDeg, encodedAngle, bothFlag ? 1 : 0, crossCount);
}
