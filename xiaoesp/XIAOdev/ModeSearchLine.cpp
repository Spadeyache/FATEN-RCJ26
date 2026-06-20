#include "ModeSearchLine.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"

static uint8_t countSilverOnColumn(camera_fb_t* fb, uint8_t col) {
    uint8_t count = 0;
    for (uint8_t y = LF_SILVER_SIDE_ROW_MIN; y <= LF_SILVER_SIDE_ROW_MAX; y++) {
        if (isSilver(updateRawGrayHSV(fb, col, y))) count++;
    }
    return count;
}

void modeSearchLineRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    uint16_t silverCount = 0;
    uint16_t blackCount = 0;

    for (uint8_t y = SEARCH_LINE_SCAN_Y_MIN; y <= SEARCH_LINE_SCAN_Y_MAX; y += SEARCH_LINE_SCAN_STEP) {
        for (uint8_t x = SEARCH_LINE_SCAN_X_MIN; x <= SEARCH_LINE_SCAN_X_MAX; x += SEARCH_LINE_SCAN_STEP) {
            cameraData d = updateRawGrayHSV(fb, x, y);
            if (isSilver(d)) silverCount++;
            else if (isBlack(d)) blackCount++;
        }
    }

    const uint8_t silverSideLeft = countSilverOnColumn(fb, LF_SILVER_SIDE_COL_LEFT);
    const uint8_t silverSideRight = countSilverOnColumn(fb, LF_SILVER_SIDE_COL_RIGHT);
    const bool sideSilverDetected =
        silverSideLeft > LF_SILVER_SIDE_THRESHOLD ||
        silverSideRight > LF_SILVER_SIDE_THRESHOLD;

    uint8_t featureId = FEAT_NONE;
    if (blackCount > SEARCH_LINE_BLACK_THRESHOLD) {
        featureId = FEAT_SEARCH_LINE_BLACK;
    } else if (silverCount > SEARCH_LINE_SILVER_THRESHOLD || sideSilverDetected) {
        featureId = FEAT_SEARCH_LINE_SILVER;
    }

    teensy.send(XIAO_REG_FEATURE, featureId);

    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=1 feat=%d sil=%d sL=%d sR=%d blk=%d",
        featureId, silverCount, silverSideLeft, silverSideRight, blackCount);
}
