#include "ModeGap.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"
#include <math.h>

// Gap mode local tuning. Keep these here so gap behavior can be tuned without
// touching the shared line-follow configuration.
static const uint8_t GAP_FRONT_ROW = 5;
static const uint8_t GAP_BACK_ROW = 60;
static const uint8_t GAP_BLACK_THRESHOLD = 5;
static const uint8_t GAP_ANGLE_CENTER_BYTE = 127;

static uint8_t rowBlackCom(camera_fb_t* fb, uint8_t row, float& comOut) {
    cameraData pixels[160] = {};
    scanRow(fb, row, 0, 159, pixels);

    uint16_t weightedSum = 0;
    uint8_t blackCount = 0;
    for (uint8_t x = 0; x < 160; x++) {
        if (isBlack(pixels[x])) {
            weightedSum += x;
            blackCount++;
        }
    }

    comOut = (blackCount > 0) ? ((float)weightedSum / blackCount) : 80.0f;
    return blackCount;
}

void modeGapRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    float frontCom = 80.0f;
    float backCom = 80.0f;
    const uint8_t frontBlack = rowBlackCom(fb, GAP_FRONT_ROW, frontCom);
    const uint8_t backBlack = rowBlackCom(fb, GAP_BACK_ROW, backCom);

    // FLAG is high when the front row sees the line. Teensy uses this to stop
    // the straight gap move and start angle alignment.
    const uint8_t frontLineSeen = (frontBlack > GAP_BLACK_THRESHOLD) ? 1 : 0;

    float angleDeg = 0.0f;
    if (frontBlack > GAP_BLACK_THRESHOLD && backBlack > GAP_BLACK_THRESHOLD) {
        const float dx = frontCom - backCom;
        const float dy = (float)GAP_BACK_ROW - (float)GAP_FRONT_ROW;
        angleDeg = atan2f(dx, dy) * (180.0f / (float)M_PI);
    }

    const uint8_t encodedAngle = (uint8_t)constrain(
        (int)(GAP_ANGLE_CENTER_BYTE + angleDeg), 0, 254);

    teensy.send(XIAO_REG_FLAG, frontLineSeen);
    teensy.send(XIAO_REG_ANGLE, encodedAngle);

    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=3 front=%d back=%d flag=%d fcom=%.1f bcom=%.1f ang=%.1f enc=%d",
        frontBlack, backBlack, frontLineSeen, frontCom, backCom, angleDeg, encodedAngle);
}
