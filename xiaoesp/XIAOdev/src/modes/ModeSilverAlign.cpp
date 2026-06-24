#include "ModeSilverAlign.h"
#include "../processing/vision.h"
#include "../config/config.h"
#include "../config/serial_print.h"
#include <math.h>

// =============================================================================
//  Mode 5 — Silver Align config (self-contained).
//
//  Method (cheap, runs only while Teensy holds this mode → no impact on the
//  line-follow path):
//    1. Walk a set of columns across the tape ROI. In each column, scan rows
//       and accumulate the Y of every silver pixel → that column's tape center.
//    2. Least-squares fit center-Y vs X over the valid columns → slope dY/dX.
//    3. tilt = atan(slope). Tape horizontal (robot perpendicular) → tilt ≈ 0.
//  Detection reuses the shared raw-silver classifier (super-bright reflection).
// =============================================================================
namespace {

// Tape scan ROI (frame is 160 x 120). Kept clear of the extreme edges.
constexpr uint8_t SA_X_MIN    = 20;
constexpr uint8_t SA_X_MAX    = 140;
constexpr uint8_t SA_COL_STEP = 6;    // sample every Nth column
constexpr uint8_t SA_Y_MIN    = 20;
constexpr uint8_t SA_Y_MAX    = 100;
constexpr uint8_t SA_ROW_STEP = 3;    // sample every Nth row within a column

// Qualifying thresholds.
constexpr uint8_t SA_MIN_SILVER_PER_COL = 2;   // silver samples to trust a column's center
constexpr uint8_t SA_MIN_VALID_COLS     = 4;   // valid columns needed → "silver seen"

constexpr uint8_t ANGLE_CENTER = 127;

// One column's silver center-of-mass in Y. Returns true if it qualifies.
bool columnSilverCenter(camera_fb_t* fb, uint8_t x, float& yCenter) {
    uint32_t sumY = 0;
    uint16_t count = 0;
    for (uint8_t y = SA_Y_MIN; y <= SA_Y_MAX; y += SA_ROW_STEP) {
        RawRgb px;
        if (sampleRawRgb(fb, x, y, px) && isSilverRaw(px)) {
            sumY += y;
            count++;
        }
    }
    if (count < SA_MIN_SILVER_PER_COL) return false;
    yCenter = (float)sumY / (float)count;
    return true;
}

}  // namespace

void modeSilverAlignRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    // Accumulate least-squares sums over the valid columns: y = m*x + b.
    float   sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f;
    uint8_t n = 0;

    for (uint8_t x = SA_X_MIN; x <= SA_X_MAX; x += SA_COL_STEP) {
        float yc;
        if (!columnSilverCenter(fb, x, yc)) continue;
        sx  += x;
        sy  += yc;
        sxx += (float)x * (float)x;
        sxy += (float)x * yc;
        n++;
    }

    float tiltDeg = 0.0f;
    const bool seen = (n >= SA_MIN_VALID_COLS);
    if (seen) {
        const float denom = (float)n * sxx - sx * sx;
        if (fabsf(denom) > 1e-3f) {
            const float slope = ((float)n * sxy - sx * sy) / denom;  // dY/dX
            tiltDeg = atan2f(slope, 1.0f) * 57.2957795f;             // 0 = horizontal tape
        }
    }

    const uint8_t encodedAngle = (uint8_t)constrain(
        (int)lroundf((float)ANGLE_CENTER + tiltDeg), 0, 254);
    const uint8_t seenFlag = seen ? XIAO_FLAG_COMMIT : 0;

    teensy.send(XIAO_REG_ANGLE, encodedAngle);
    teensy.send(XIAO_REG_FLAG,  seenFlag);

    digitalWrite(LED_BUILTIN, seen ? LOW : HIGH);  // ESP32 LED active-LOW

    SPRINTF(SPRINT_RESULTS, "[SA]",
        "mode=5 cols=%d seen=%d tilt=%.1f enc=%d",
        n, seen ? 1 : 0, tiltDeg, encodedAngle);
}
