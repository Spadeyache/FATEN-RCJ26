#pragma once

// Hardware-dependent settings for this XIAO ESP32-S3 camera build.
// Keep board pins, serial speeds, frame geometry, and camera ROI geometry here.

#include <Arduino.h>

// Serial ports.
#define SERIAL_DEBUG_BAUD    115200
#define SERIAL_TEENSY_BAUD   4000000
#define SERIAL_TEENSY_RX_PIN D7
#define SERIAL_TEENSY_TX_PIN D6

// Camera frame.
#define CAMERA_FRAME_WIDTH   160
#define CAMERA_FRAME_HEIGHT  120
#define CAMERA_FRAME_BPP     2
#define CAMERA_FRAME_BYTES   (CAMERA_FRAME_WIDTH * CAMERA_FRAME_HEIGHT * CAMERA_FRAME_BPP)

// Line-follow arc ROI. These are camera/mirror dependent.
#define LF_ARC_MAX_SAMPLES      340
#define LF_ARC_X_SHIFT          0
#define LF_ARC_Y_SHIFT          7
#define LF_ARC_TOP_X            (80 + LF_ARC_X_SHIFT)
#define LF_ARC_TOP_Y            (3 + LF_ARC_Y_SHIFT)
#define LF_ARC_LEFT_X           (25 + 3 + LF_ARC_X_SHIFT)
#define LF_ARC_RIGHT_X          (135 - 3 + LF_ARC_X_SHIFT)
#define LF_ARC_SIDE_Y           (33 + LF_ARC_Y_SHIFT)
#define LF_ARC_BOTTOM_Y         (84 + LF_ARC_Y_SHIFT)
#define LF_ARC_BOTTOM_LEFT_X    (40 + LF_ARC_X_SHIFT)
#define LF_ARC_BOTTOM_RIGHT_X   (120 + LF_ARC_X_SHIFT)
#define LF_ARC_SAMPLE_SPACING   1.0f

// Line-follow side silver scan ROI.
#define LF_SILVER_COL_LEFT      33
#define LF_SILVER_COL_RIGHT     127
#define LF_SILVER_ROW_MIN       0
#define LF_SILVER_ROW_MAX       70
