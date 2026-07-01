#pragma once

#include "../drivers/yacheEncodedSerial.h"
#include "esp_camera.h"

// Mode 6: detect a black line/point on the front arc near image center.
// Sends FEAT_CENTER_POINT_BLACK when the midpoint of a front-arc black run is
// inside LF_CENTER_X +/- CENTER_POINT_BAND_HALF_PX.
void modeCenterPointRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
