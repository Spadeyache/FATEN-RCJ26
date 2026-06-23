#pragma once
#include "esp_camera.h"
#include "../drivers/yacheEncodedSerial.h"

// Mode 3 — Line Angle
//
// Scans two horizontal rows to find any line and its slope relative to the robot.
// Each row finds the largest contiguous black chunk; its CoM is the focused point.
//
// Sends every frame:
//   XIAO_REG_ANGLE  — slope of mid→bottom vector, 0-254 (127 = 0°, ≈1 step/deg)
//   XIAO_REG_FLAG   — bit0 = both rows have a qualifying black chunk (both-rows flag)
//   XIAO_REG_COM    — total arc crossing count (same ROI geometry as line follow)
void modeLineAngleRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
