#pragma once
#include "esp_camera.h"
#include "../drivers/yacheEncodedSerial.h"

// Mode 3 — Line Angle (gap-crossing helper)
//
// Runs the shared arc point detection (lc_detectCrossings) and steers off the
// raw crossings WITHOUT the in/out classifier — just enough to follow a line
// straight across a gap.
//
// Point selection:
//   - >= 2 points : keep the two largest (by width).
//   - == 1 point  : scan an inner circle (radius LA_CIRCLE_RADIUS px) around it
//                   and look for a second continuous black point.
//   With two points the slope is taken base -> tip, where the base is the lower
//   point in the frame (larger pixelY = nearer the robot / lower power).
//
// Sends every frame:
//   XIAO_REG_ANGLE  — slope of base->tip vector, 0-254 (127 = 0°, ≈1 step/deg)
//   XIAO_REG_FLAG   — bit0 = at least one point was detected (guaranteed point)
//   XIAO_REG_COM    — average pixelY of the two points, 0-254 (mode-scoped)
void modeLineAngleRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
