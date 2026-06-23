#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include "esp_camera.h"

// XIAO ESP32-S3 Sense pin definitions
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

// Camera configuration for vision processing
#define FRAME_SIZE  FRAMESIZE_QVGA  // 320x240
#define PIX_FORMAT  PIXFORMAT_RGB565

// Function declarations
bool Camera_Init();
camera_fb_t* Camera_Grab();
void Camera_Return(camera_fb_t* fb);

#endif // CAMERA_CONFIG_H
