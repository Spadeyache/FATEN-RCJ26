#include "ModeCenterPoint.h"

#include "../processing/vision.h"
#include "../config/config.h"
#include "../config/serial_print.h"

#include <Arduino.h>
#include <math.h>

namespace {

constexpr int MAX_SAMPLES = LF_ARC_MAX_SAMPLES;
constexpr float ARC_PI = 3.14159265358979323846f;
constexpr uint8_t BAND_HALF_PX = 10;
constexpr uint8_t RUN_MIN_LEN = 3;
constexpr uint8_t SAMPLE_HALF_W = 5;
constexpr uint8_t SAMPLE_HALF_H = 2;
constexpr uint8_t SAMPLE_BLACK_HITS = 20;

uint8_t s_px[MAX_SAMPLES];
uint8_t s_py[MAX_SAMPLES];
uint8_t s_black[MAX_SAMPLES];
int s_sampleCount = 0;
bool s_geometryReady = false;

void addSample(int x, int y) {
    if (s_sampleCount >= MAX_SAMPLES) return;
    s_px[s_sampleCount] = (uint8_t)constrain(x, 0, CAMERA_FRAME_WIDTH - 1);
    s_py[s_sampleCount] = (uint8_t)constrain(y, 0, CAMERA_FRAME_HEIGHT - 1);
    s_sampleCount++;
}

void buildFrontArcGeometry() {
    if (s_geometryReady) return;
    s_sampleCount = 0;

    const float cx = (float)LF_ARC_TOP_X;
    const float topY = (float)LF_ARC_TOP_Y;
    const float sideX = (float)LF_ARC_RIGHT_X;
    const float sideY = (float)LF_ARC_SIDE_Y;
    const float dx = sideX - cx;
    const float cy = (sideY * sideY - topY * topY + dx * dx) / (2.0f * (sideY - topY));
    const float r = cy - topY;

    const float rightA = atan2f((float)LF_ARC_SIDE_Y - cy, (float)LF_ARC_RIGHT_X - cx);
    const float leftA = atan2f((float)LF_ARC_SIDE_Y - cy, (float)LF_ARC_LEFT_X - cx);
    float sweep = leftA - rightA;
    while (sweep > 0.0f) sweep -= 2.0f * ARC_PI;

    const int steps = max(1, (int)lroundf(fabsf(sweep) * r / LF_ARC_SAMPLE_SPACING));
    for (int i = 0; i <= steps; i++) {
        const float t = (float)i / (float)steps;
        const float a = rightA + sweep * t;
        addSample((int)lroundf(cx + cosf(a) * r),
                  (int)lroundf(cy + sinf(a) * r));
    }

    s_geometryReady = true;
}

bool sampleRawAt(camera_fb_t* fb, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (!fb || !fb->buf) return false;
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return false;
    rgb565To888(unpackRGB565(fb->buf, (size_t)y * fb->width + x), r, g, b);
    return true;
}

bool frontArcSampleBlack(camera_fb_t* fb, uint8_t x, uint8_t y) {
    uint8_t blackHits = 0;
    uint8_t saturatedHits = 0;

    for (int dy = -(int)SAMPLE_HALF_H; dy <= (int)SAMPLE_HALF_H; dy++) {
        for (int dx = -(int)SAMPLE_HALF_W; dx <= (int)SAMPLE_HALF_W; dx++) {
            uint8_t r, g, b;
            if (!sampleRawAt(fb, (int)x + dx, (int)y + dy, r, g, b)) continue;
            if (r >= SILVER_RAW_R_MIN && g >= SILVER_RAW_G_MIN && b >= SILVER_RAW_B_MIN) {
                saturatedHits++;
                continue;
            }

            rgb888Calibration(r, g, b);
            if (rgbToGray(r, g, b) <= BLACK_GRAY_MAX) blackHits++;
        }
    }

    if (blackHits == 0) return false;
    const int threshold = (int)SAMPLE_BLACK_HITS - (int)saturatedHits;
    return blackHits >= threshold;
}

bool centeredBlackRun(camera_fb_t* fb, uint8_t& midpointX, uint8_t& runLen) {
    buildFrontArcGeometry();
    midpointX = (uint8_t)LF_CENTER_X;
    runLen = 0;
    if (s_sampleCount <= 0) return false;

    for (int i = 0; i < s_sampleCount; i++) {
        s_black[i] = frontArcSampleBlack(fb, s_px[i], s_py[i]) ? 1 : 0;
    }

    for (int i = 0; i < s_sampleCount;) {
        if (!s_black[i]) {
            i++;
            continue;
        }

        const int start = i;
        while (i < s_sampleCount && s_black[i]) i++;
        const int len = i - start;
        if (len < RUN_MIN_LEN) continue;

        const int mid = start + len / 2;
        const int x = s_px[mid];
        if (abs(x - (int)LF_CENTER_X) <= BAND_HALF_PX) {
            midpointX = (uint8_t)x;
            runLen = (uint8_t)min(len, 255);
            return true;
        }
    }

    return false;
}

}  // namespace

void modeCenterPointRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    uint8_t midpointX = (uint8_t)LF_CENTER_X;
    uint8_t runLen = 0;
    const bool seen = centeredBlackRun(fb, midpointX, runLen);

    teensy.send(XIAO_REG_FEATURE, seen ? FEAT_CENTER_POINT_BLACK : FEAT_NONE);
    teensy.send(XIAO_REG_COM, midpointX);
    teensy.send(XIAO_REG_FLAG, seen ? XIAO_FLAG_COMMIT : 0);

    digitalWrite(LED_BUILTIN, seen ? LOW : HIGH);
    SPRINTF(SPRINT_SERIAL_IN, "[CP]", "seen=%d x=%u len=%u", seen ? 1 : 0, midpointX, runLen);
}
