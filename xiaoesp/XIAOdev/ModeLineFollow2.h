#pragma once
#include "esp_camera.h"
#include "YacheEncodedSerial.h"

// Mode 0b — Line Follow 2 (clean restructure of the line-follow logic).
//
//  Two scan rows (config.h LF2_ROW_*):
//    NEAR row (70): black center-of-mass → line error (0..254, 127 = centre);
//                   too little black → "no line" (error = centre, FEAT_LINE_LOST).
//    FAR  row (40): edge cases — red → FEAT_RED; black saturation → light the LED.
//
//  All of this mode's logic lives in ModeLineFollow2.cpp; it only calls the
//  shared vision helpers (scanRow / isBlack / isRed). Green markers, committed
//  turns and in/out tracking are intentionally NOT handled here — that logic
//  stays in modeLineFollowRun (kept but unused) and may be re-added later.
void modeLineFollow2Run(camera_fb_t* fb, YacheEncodedSerial& teensy);
