#pragma once
#include "esp_camera.h"
#include "../drivers/yacheEncodedSerial.h"

// Mode 2 — No-Green Intersection
// Scans NOGI_SCAN_ROW_COUNT rows starting from y=0 (front-facing, far-field view).
// Sends FEAT_NOGI_INTERSECT on XIAO_REG_FEATURE when the total black-pixel count
// across all scanned rows exceeds NOGI_BLACK_THRESHOLD; otherwise FEAT_NONE.
void modeNoGIRun(camera_fb_t* fb, YacheEncodedSerial& teensy);

