#include "ModeEvacColorMask.h"
#include "../processing/vision.h"
#include "../config/config.h"
#include "../config/serial_print.h"
#include "../stream/XiaoStream.h"
#include <Arduino.h>

// =============================================================================
//  Mode 5 - Evac color mask
//
//  Simple evac entrance/exit helper:
//    1. Detect silver using the same side-column raw-silver scan as line follow.
//    2. Count black pixels on row 45. If the count is high, raise the black flag.
//
//  No tape angle is calculated or sent in this mode.
// =============================================================================
namespace {

constexpr uint8_t SILVER_COL_LEFT = LF_SILVER_COL_LEFT;
constexpr uint8_t SILVER_COL_RIGHT = LF_SILVER_COL_RIGHT;
constexpr uint8_t SILVER_ROW_MIN = LF_SILVER_ROW_MIN;
constexpr uint8_t SILVER_ROW_MAX = LF_SILVER_ROW_MAX;
constexpr uint8_t SILVER_THRESHOLD = 9;  // evac-only; line-follow keeps its own

constexpr uint8_t BLACK_ROW = 45;
// Inset from the arc edges: at row 45 the extreme columns fall in the dark
// mirror/frame border (shadow) and read as black. Line-follow tolerates that
// because its black gate is high; this mode's is low (5), so the shadow alone
// would trip it. Keep the scan inside the clean part of the view.
constexpr uint8_t BLACK_EDGE_MARGIN = 12;
constexpr uint8_t BLACK_X_MIN = LF_ARC_LEFT_X + BLACK_EDGE_MARGIN;   // 40
constexpr uint8_t BLACK_X_MAX = LF_ARC_RIGHT_X - BLACK_EDGE_MARGIN;  // 120
constexpr uint8_t BLACK_THRESHOLD = 40;

uint8_t countSilverOnColumn(camera_fb_t* fb, uint8_t col) {
    uint8_t count = 0;
    for (uint8_t y = SILVER_ROW_MIN; y <= SILVER_ROW_MAX; y++) {
        RawRgb px;
        if (sampleRawRgb(fb, col, y, px) && isSilverRaw(px)) count++;
    }
    return count;
}

// Black pixel count on the single scan row BLACK_ROW, across the arc ROI x
// extent. Identical to line-follow's scanColorRow: scanRow -> updateRawGrayHSV
// (mapped/calibrated) -> isBlack (gray <= BLACK_GRAY_MAX).
uint8_t countBlackOnRow(camera_fb_t* fb) {
    cameraData rowPixels[160] = {};
    scanRow(fb, BLACK_ROW, BLACK_X_MIN, BLACK_X_MAX, rowPixels);

    uint8_t count = 0;
    for (uint8_t x = BLACK_X_MIN; x <= BLACK_X_MAX; x++) {
        if (isBlack(rowPixels[x])) count++;
    }
    return count;
}

}  // namespace

void modeEvacColorMaskRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    xs_beginSensorFrame();

    const uint8_t silverLeft = countSilverOnColumn(fb, SILVER_COL_LEFT);
    const uint8_t silverRight = countSilverOnColumn(fb, SILVER_COL_RIGHT);
    const bool silverDetected =
        silverLeft > SILVER_THRESHOLD || silverRight > SILVER_THRESHOLD;

    const uint8_t blackCount = countBlackOnRow(fb);
    const bool blackDetected = blackCount > BLACK_THRESHOLD;

    uint8_t flags = 0;
    if (silverDetected) flags |= XIAO_FLAG_COMMIT;
    if (blackDetected) flags |= XIAO_FLAG_BOTTOM_LINE;

    const uint8_t featureId = silverDetected ? FEAT_SILVER : FEAT_NONE;

    teensy.send(XIAO_REG_FEATURE, featureId);
    teensy.send(XIAO_REG_FLAG, flags);
    teensy.send(XIAO_REG_COM, blackCount);

    if (silverLeft > SILVER_THRESHOLD) {
        xs_setSensorSide(XS_LEFT, XS_SILVER, XS_PRIO_SILVER);
    }
    if (silverRight > SILVER_THRESHOLD) {
        xs_setSensorSide(XS_RIGHT, XS_SILVER, XS_PRIO_SILVER);
    }
    if (blackDetected) {
        xs_setSensorBoth(XS_BLACK, XS_PRIO_LINE);
    }

    xs_storeEvacTapeDebug(
        silverDetected || blackDetected,
        silverDetected ? "silver" : (blackDetected ? "black" : "none"),
        blackCount,
        (uint16_t)silverLeft + (uint16_t)silverRight,
        blackCount,
        BLACK_X_MIN,
        BLACK_ROW,
        BLACK_X_MAX,
        BLACK_ROW,
        nullptr,
        nullptr,
        0);

    digitalWrite(LED_BUILTIN, (silverDetected || blackDetected) ? LOW : HIGH);

    SPRINTF(SPRINT_RESULTS, "[SA]",
        "mode=5 silver=%d sL=%d sR=%d black=%d blackCnt=%d row=%d feat=%d flag=%u",
        silverDetected ? 1 : 0,
        silverLeft,
        silverRight,
        blackDetected ? 1 : 0,
        blackCount,
        BLACK_ROW,
        featureId,
        flags);
}
