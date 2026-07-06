#include "ModeGap.h"
#include "../processing/vision.h"
#include "../config/config.h"
#include "../config/serial_print.h"

#include <Arduino.h>
#include <math.h>

namespace {
constexpr uint8_t GAP_FRONT_ROW = 5;
constexpr uint8_t GAP_BACK_ROW = 60;
constexpr uint8_t GAP_BLACK_THRESHOLD = 5;
constexpr uint8_t GAP_ANGLE_CENTER_BYTE = 127;

uint8_t rowBlackCom(camera_fb_t* fb, uint8_t row, float& comOut) {
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

    comOut = blackCount ? ((float)weightedSum / (float)blackCount) : 80.0f;
    return blackCount;
}
}

void modeGapRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    float frontCom = 80.0f;
    float backCom = 80.0f;
    const uint8_t frontBlack = rowBlackCom(fb, GAP_FRONT_ROW, frontCom);
    const uint8_t backBlack = rowBlackCom(fb, GAP_BACK_ROW, backCom);

    const uint8_t frontLineSeen = (frontBlack > GAP_BLACK_THRESHOLD) ? 1 : 0;

    float angleDeg = 0.0f;
    const float dx = frontCom - backCom;
    if (frontBlack > 0 && backBlack > 0) {
        const float dy = (float)GAP_BACK_ROW - (float)GAP_FRONT_ROW;
        angleDeg = atan2f(dx, dy) * (180.0f / (float)M_PI);
    }

    const uint8_t encodedAngle = (uint8_t)constrain(
        (int)roundf((float)GAP_ANGLE_CENTER_BYTE + angleDeg), 0, 254);

    teensy.send(XIAO_REG_FLAG, frontLineSeen ? XIAO_FLAG_COMMIT : 0);
    teensy.send(XIAO_REG_ANGLE, encodedAngle);

    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=3 front=%d back=%d flag=%d fcom=%.1f bcom=%.1f dx=%.1f ang=%.1f enc=%d",
        frontBlack, backBlack, frontLineSeen, frontCom, backCom, dx, angleDeg, encodedAngle);
}
