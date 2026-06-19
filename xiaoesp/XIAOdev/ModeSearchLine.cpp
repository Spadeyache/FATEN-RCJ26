#include "ModeSearchLine.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"

void modeSearchLineRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    // ── 1. Scan the configured 2D region with a stride ───────────────────────
    uint16_t silverCount = 0;
    uint16_t blackCount  = 0;

    for (uint8_t y = SEARCH_LINE_SCAN_Y_MIN; y <= SEARCH_LINE_SCAN_Y_MAX; y += SEARCH_LINE_SCAN_STEP) {
        for (uint8_t x = SEARCH_LINE_SCAN_X_MIN; x <= SEARCH_LINE_SCAN_X_MAX; x += SEARCH_LINE_SCAN_STEP) {
            cameraData d = updateRawGrayHSV(fb, x, y);
            if      (isSilver(d)) silverCount++;
            else if (isBlack(d))  blackCount++;
        }
    }

    // ── 2. Decide feature (black takes priority) ─────────────────────────────
    uint8_t featureId = FEAT_NONE;

    if      (blackCount  > SEARCH_LINE_BLACK_THRESHOLD)  featureId = FEAT_SEARCH_LINE_BLACK;
    else if (silverCount > SEARCH_LINE_SILVER_THRESHOLD) featureId = FEAT_SEARCH_LINE_SILVER;

    teensy.send(XIAO_REG_FEATURE, featureId);

    // ── 3. Debug output ──────────────────────────────────────────────────────
    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=1 feat=%d sil=%d blk=%d",
        featureId, silverCount, blackCount);
}
