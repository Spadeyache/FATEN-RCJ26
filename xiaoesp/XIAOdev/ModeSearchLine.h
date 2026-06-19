#pragma once
#include "esp_camera.h"
#include "YacheEncodedSerial.h"

// Mode 1 — SearchLine
// Scans the rectangular region [SEARCH_LINE_SCAN_X_MIN..SEARCH_LINE_SCAN_X_MAX] x
// [SEARCH_LINE_SCAN_Y_MIN..SEARCH_LINE_SCAN_Y_MAX], sampling every SEARCH_LINE_SCAN_STEP pixels.
// Sends FEAT_SEARCH_LINE_SILVER or FEAT_SEARCH_LINE_BLACK on XIAO_REG_FEATURE when a threshold
// is exceeded; otherwise sends FEAT_NONE. Black takes priority over silver.
void modeSearchLineRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
