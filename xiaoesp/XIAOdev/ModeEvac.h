#pragma once
#include "esp_camera.h"
#include "YacheEncodedSerial.h"

// Mode 1 — Evac
// Scans the rectangular region [EVAC_SCAN_X_MIN..EVAC_SCAN_X_MAX] x
// [EVAC_SCAN_Y_MIN..EVAC_SCAN_Y_MAX], sampling every EVAC_SCAN_STEP pixels.
// Sends FEAT_EVAC_SILVER or FEAT_EVAC_BLACK on XIAO_REG_FEATURE when a threshold
// is exceeded; otherwise sends FEAT_NONE. Black takes priority over silver.
void modeEvacRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
