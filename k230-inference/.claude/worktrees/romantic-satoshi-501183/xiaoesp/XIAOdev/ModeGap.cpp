#include "ModeGap.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"
#include <math.h>

void modeGapRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    // ── 1. Collect per-row black centroids for regression ─────────────────────
    // Samples: (y, centerX) for each row that contains black pixels.
    float sampleY[GAP_SCAN_ROW_COUNT];
    float sampleX[GAP_SCAN_ROW_COUNT];
    uint8_t nSamples = 0;

    cameraData pixels[160] = {};

    for (uint8_t i = 0; i < GAP_SCAN_ROW_COUNT; i++) {
        uint8_t row = GAP_SCAN_ROW_START + i;
        scanRow(fb, row, 0, 159, pixels);

        int32_t weightedSum = 0;
        uint8_t blackCount  = 0;
        for (uint8_t c = 0; c < 160; c++) {
            if (isBlack(pixels[c])) { weightedSum += c; blackCount++; }
        }

        if (blackCount > 0) {
            sampleY[nSamples] = (float)row;
            sampleX[nSamples] = (float)weightedSum / blackCount;
            nSamples++;
        }
    }

    // ── 2. Also send COM from the standard SCAN_ROW ───────────────────────────
    scanRow(fb, SCAN_ROW, SCAN_COL_MIN, SCAN_COL_MAX, pixels);
    int32_t wSum = 0; uint8_t bCnt = 0;
    for (uint8_t c = SCAN_COL_MIN; c <= SCAN_COL_MAX; c++) {
        if (isBlack(pixels[c])) { wSum += c; bCnt++; }
    }
    float com = (bCnt > 0) ? (float)wSum / bCnt : (SCAN_COL_MIN + SCAN_COL_MAX) / 2.0f;
    uint8_t scaledCOM = (uint8_t)constrain(
        map((long)(com * 10), (long)(SCAN_COL_MIN * 10), (long)(SCAN_COL_MAX * 10), 0, 254), 0, 254);
    teensy.send(XIAO_REG_COM, scaledCOM);

    // ── 3. Least-squares slope: x = a*y + b  →  a = (n·Σxy − Σx·Σy) / (n·Σy² − (Σy)²)
    //      Then angle = atan(a) in degrees.
    float angleDeg = 0.0f;
    uint8_t encodedAngle = GAP_ANGLE_CENTER;

    if (nSamples >= 2) {
        float sumY = 0, sumX = 0, sumYY = 0, sumXY = 0;
        for (uint8_t i = 0; i < nSamples; i++) {
            sumY  += sampleY[i];
            sumX  += sampleX[i];
            sumYY += sampleY[i] * sampleY[i];
            sumXY += sampleX[i] * sampleY[i];
        }
        float denom = (float)nSamples * sumYY - sumY * sumY;
        if (fabsf(denom) > 1e-4f) {
            float slope = ((float)nSamples * sumXY - sumX * sumY) / denom;
            angleDeg    = atan(slope) * (180.0f / (float)M_PI);
        }
        encodedAngle = (uint8_t)constrain(
            GAP_ANGLE_CENTER + (int)(angleDeg * GAP_ANGLE_SCALE), 0, 254);
    }

    teensy.send(XIAO_REG_ANGLE, encodedAngle);

    // ── 4. Debug output ───────────────────────────────────────────────────────
    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=3 ang=%.1f enc=%d samples=%d com=%.1f",
        angleDeg, encodedAngle, nSamples, com);
}
