#pragma once
#include "esp_camera.h"
#include "YacheEncodedSerial.h"

// Mode 3 — Gap (line-angle estimation)
//
// Scans GAP_SCAN_ROW_COUNT rows starting at GAP_SCAN_ROW_START and computes the
// angle of the detected line using least-squares linear regression over per-row
// black-pixel centroids.
//
// Sends on XIAO_REG_ANGLE: 0–254 where 127 = 0°, ±90° ≈ ±127 counts.
// Also sends COM on XIAO_REG_COM (from SCAN_ROW) so the Teensy can keep PID live.
void modeGapRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
