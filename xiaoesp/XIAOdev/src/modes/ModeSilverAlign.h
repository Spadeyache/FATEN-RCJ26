#pragma once
#include "esp_camera.h"
#include "../drivers/yacheEncodedSerial.h"

// Mode 5 — Silver Align
//
// Used at evacuation-zone entry to make the robot enter perpendicular to the
// silver tape. The tape reads as a band of bright/gray silver pixels (with
// saturated LED-reflection spots). This mode fits the tape's center-row across
// several columns and reports its tilt — when the robot is perpendicular the
// tape is horizontal in the image (tilt ≈ 0).
//
// Sends every frame:
//   XIAO_REG_ANGLE  = 127 + signed tape tilt in degrees (127 = level/perpendicular)
//   XIAO_REG_FLAG   bit0 = silver tape seen (enough columns carried silver)
//
// Self-contained: shares nothing with the line-follow / other modes.
void modeSilverAlignRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
