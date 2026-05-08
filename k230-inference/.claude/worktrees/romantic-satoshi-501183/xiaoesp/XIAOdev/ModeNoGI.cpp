#include "ModeNoGI.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"

void modeNoGIRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    // ── 1. Scan first NOGI_SCAN_ROW_COUNT rows across the full frame width ────
    cameraData pixels[160] = {};
    uint16_t totalBlack = 0;

    // for (uint8_t row = 0; row < NOGI_SCAN_ROW_COUNT; row++) {    // preform on multiple rows
        scanRow(fb, /*row*/NOGI_SCAN_ROW, SCAN_COL_MIN, SCAN_COL_MAX, pixels);
        for (uint8_t c = SCAN_COL_MIN; c <= SCAN_COL_MAX; c++) {
            if (isBlack(pixels[c])) totalBlack++;
        }
    // }

    // ── 2. Decide feature ─────────────────────────────────────────────────────
    uint8_t featureId = (totalBlack > NOGI_BLACK_THRESHOLD) ? FEAT_NOGI_INTERSECT : FEAT_NONE;
    teensy.send(XIAO_REG_FEATURE, featureId);

    // ── 3. Debug output ───────────────────────────────────────────────────────
    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=2 feat=%d blk=%d",
        featureId, totalBlack);
}
