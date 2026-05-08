#include "ModeEvac.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"

void modeEvacRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    // ── 1. Scan EVAC_SCAN_ROW across the full frame width ────────────────────
    cameraData pixels[160] = {};
    scanRow(fb, EVAC_SCAN_ROW, SCAN_COL_MIN, SCAN_COL_MAX, pixels);

    // ── 2. Count silver and black pixels ─────────────────────────────────────
    uint8_t silverCount = 0;
    uint8_t blackCount  = 0;

    for (uint8_t c = SCAN_COL_MIN; c <= SCAN_COL_MAX; c++) {
        if (isSilver(pixels[c])) silverCount++;
        if (isBlack(pixels[c]))  blackCount++;
    }

    // ── 3. Decide feature (silver takes priority) ─────────────────────────────
    uint8_t featureId = FEAT_NONE;

    if      (silverCount > EVAC_SILVER_THRESHOLD) featureId = FEAT_EVAC_SILVER;
    else if (blackCount  > EVAC_BLACK_THRESHOLD)  featureId = FEAT_EVAC_BLACK;

    teensy.send(XIAO_REG_FEATURE, featureId);

    // ── 4. Debug output ───────────────────────────────────────────────────────
    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=1 feat=%d sil=%d blk=%d",
        featureId, silverCount, blackCount);
}
