#pragma once
#include "esp_camera.h"
#include "../drivers/yacheEncodedSerial.h"

// Mode 5 - Evac tape align / classify
//
// Used at evacuation-zone entrance/exit to classify the big tape as silver or
// black and report its angle. Silver detection starts from the same saturated
// reflection pixels used by line-follow, then grows a connected mask through
// nearby silver-body pixels. Black detection does the same with dark pixels.
//
// Sends every frame:
//   XIAO_REG_ANGLE = 127 + signed tape tilt in degrees
//                    (127 = level/perpendicular)
//   XIAO_REG_FLAG  bit0 = tape seen, bit1 = silver, bit2 = black
//   XIAO_REG_COM   confidence-ish connected pixel count, clamped to 254
//
// Self-contained: shares nothing with the line-follow / other modes.
void modeSilverAlignRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
