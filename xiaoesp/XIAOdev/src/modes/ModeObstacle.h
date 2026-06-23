#pragma once
#include "esp_camera.h"
#include "../drivers/yacheEncodedSerial.h"

// Mode 4 — Obstacle re-acquire
//
// Used after the robot has driven around an obstacle, to confirm the line is
// back in view and report its rough direction. Two outputs:
//
//   XIAO_REG_FLAG   bit0 = "see line": the TOP ARC of the box (only the arc,
//                          not the tilted sides or bottom edge) contains at
//                          least OBS_ARC_BLACK_THRESHOLD black samples.
//
//   XIAO_REG_ANGLE  = 127 + signed line tilt in degrees (folded to [-90,90],
//                     127 = 0° = vertical). Computed from the highest-priority
//                     crossing on the full box (tilted sides first, then arc /
//                     bottom, tie-broken by chunk size) by tracing an inner
//                     circle to find which way the line continues. 0° (127) if
//                     no crossing or no traceable continuation.
void modeObstacleRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
