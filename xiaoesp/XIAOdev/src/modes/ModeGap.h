#pragma once

#include "esp_camera.h"
#include "../drivers/yacheEncodedSerial.h"

void modeGapRun(camera_fb_t* fb, YacheEncodedSerial& teensy);
