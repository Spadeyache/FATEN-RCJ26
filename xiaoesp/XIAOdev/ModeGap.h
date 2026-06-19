#pragma once
#include "esp_camera.h"
#include "YacheEncodedSerial.h"

// Mode 3 — Gap movement helper
//
// Scans two fixed rows configured locally in ModeGap.cpp. Sends:
//   XIAO_REG_FLAG  = 1 when the front row sees enough black pixels
//   XIAO_REG_ANGLE = 127 + signed angle in degrees from back-row COM to front-row COM
//
void modeGapRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
