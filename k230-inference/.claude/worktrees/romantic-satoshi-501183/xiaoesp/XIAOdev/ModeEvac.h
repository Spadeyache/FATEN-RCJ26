#pragma once
#include "esp_camera.h"
#include "YacheEncodedSerial.h"

// Mode 1 — Evac
// Scans EVAC_SCAN_ROW across the full image width.
// Sends FEAT_EVAC_SILVER or FEAT_EVAC_BLACK on XIAO_REG_FEATURE when a threshold
// is exceeded; otherwise sends FEAT_NONE. Silver takes priority over black.
void modeEvacRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
