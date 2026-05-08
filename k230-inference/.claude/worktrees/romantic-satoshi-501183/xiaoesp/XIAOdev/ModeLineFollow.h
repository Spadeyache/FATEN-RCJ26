#pragma once
#include "esp_camera.h"
#include "YacheEncodedSerial.h"

// Mode 0 — Line Follow
// Scans SCAN_ROW across [SCAN_COL_MIN, SCAN_COL_MAX].
// Sends COM on XIAO_REG_COM and the highest-priority feature on XIAO_REG_FEATURE.
void modeLineFollowRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
