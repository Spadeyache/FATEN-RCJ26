#pragma once
#include "esp_camera.h"
#include "../drivers/yacheEncodedSerial.h"

// Mode 5 - Evac color mask
//
// Used at evacuation-zone entrance/exit for two simple checks:
//   - silver: same side-column raw-silver scan as line-follow
//   - black: count black pixels on row 45
//
// Sends every frame:
//   XIAO_REG_FEATURE = FEAT_SILVER when silver is seen, else FEAT_NONE
//   XIAO_REG_FLAG    bit0 = silver seen, bit2 = row-45 black threshold hit
//   XIAO_REG_COM     row-45 black pixel count
//
// No angle is calculated or sent.
void modeEvacColorMaskRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
