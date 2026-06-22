// serialCameraStream.ino  —  TEMPORARY DEBUG TOOL
// Captures full JPEG frames (the same proven method as cameraStream.ino) and
// streams them over USB Serial as base64 with START/END markers at ~1 fps.
// The bottom-left crop is applied in the browser viewer, so the capture path
// stays simple and reliable.
//
// Open log/serialCameraViewer.html in Chrome, click "Connect Serial".
// Set CROP_PERCENT there to change how much of the corner you see.

#include "esp_camera.h"
#include "mbedtls/base64.h"
#include "esp_log.h"

// ── Stream timing ───────────────────────────────────────────────────────────
static const uint32_t FRAME_INTERVAL_MS = 3000;   // one frame every 3 s — max reliability
static const uint32_t SERIAL_BAUD       = 921600; // USB-CDC: label only; match in viewer

// ── Camera pins (XIAO ESP32-S3 Sense) ───────────────────────────────────────
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

// ── Camera init (mirrors the working cameraStream.ino) ──────────────────────
static void initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;     // sensor-side JPEG = the reliable path
  config.frame_size   = FRAMESIZE_VGA;      // 640x480 (highest the OV2640 streams smoothly)
  config.jpeg_quality = 10;                 // 0-63, lower = better quality
  config.fb_count     = 2;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("ERR: camera init 0x%x\n", err);
    while (true) delay(1000);
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_brightness(s, 0);
  s->set_contrast(s,   0);
  s->set_saturation(s, 0);
  s->set_hmirror(s,    0);
  s->set_vflip(s,      0);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1500);

  // Keep the ESP-IDF logger off our Serial stream so it can't corrupt frames.
  Serial.setDebugOutput(false);
  esp_log_level_set("*", ESP_LOG_NONE);

  initCamera();
  Serial.println("STREAM_READY jpeg full-frame, crop applied in viewer");
}

void loop() {
  static uint32_t last_frame = 0;
  uint32_t now = millis();
  if (now - last_frame < FRAME_INTERVAL_MS) return;
  last_frame = now;

  // ── 1. Grab JPEG frame straight from the sensor ────────────────────────────
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || fb->format != PIXFORMAT_JPEG) {
    if (fb) esp_camera_fb_return(fb);
    Serial.println("ERR:capture");
    return;
  }

  // ── 2. Base64 encode the JPEG ──────────────────────────────────────────────
  size_t   b64_max = ((fb->len + 2) / 3) * 4 + 1;
  uint8_t *b64_buf = (uint8_t *)ps_malloc(b64_max);
  if (!b64_buf) {
    esp_camera_fb_return(fb);
    Serial.println("ERR:b64_alloc");
    return;
  }

  size_t b64_len = 0;
  int rc = mbedtls_base64_encode(b64_buf, b64_max, &b64_len, fb->buf, fb->len);
  esp_camera_fb_return(fb);
  if (rc != 0 || b64_len == 0) {
    free(b64_buf);
    Serial.printf("ERR:b64_encode rc=%d\n", rc);
    return;
  }

  // ── 3. Transmit:  FRAME_START:<len>\n <base64> \nFRAME_END\n ────────────────
  Serial.printf("FRAME_START:%u\n", (unsigned)b64_len);
  Serial.write(b64_buf, b64_len);
  Serial.print("\nFRAME_END\n");
  Serial.flush();
  free(b64_buf);
}
