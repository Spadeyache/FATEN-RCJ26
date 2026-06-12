#include <Arduino.h>
#include "esp_log.h"
#include "YacheEncodedSerial.h"
#include "camera_config.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"
#include "ModeLineFollow.h"
#include "ModeEvac.h"
#include "ModeNoGI.h"
#include "ModeGap.h"
// #include "wifi_config.h"

// ── Output mode ────────────────────────────────────────────────────────────────
// STREAM  →  binary camera frames over Serial (use with viewer)
// LOG     →  human-readable debug text over Serial (use with serial monitor)
// #define OUTPUT_STREAM
#define OUTPUT_LOG

// Stream FPS cap — limits USB interrupt pressure on Core 1.
// Lower = less impact on loop speed. 10 is a good balance.
#define STREAM_FPS 10
// ──────────────────────────────────────────────────────────────────────────────

#if defined(OUTPUT_STREAM) == defined(OUTPUT_LOG)
  #error "Define exactly one of OUTPUT_STREAM or OUTPUT_LOG"
#endif

// ── Serial link to Teensy ──────────────────────────────────────────────────────
YacheEncodedSerial teensy(Serial1);

// ── Dual-core streaming globals ────────────────────────────────────────────────
static constexpr size_t FRAME_BYTES = 160UL * 120UL * 2UL;

static uint8_t*  streamBuf[2]    = {nullptr, nullptr};
static uint16_t  streamW[2], streamH[2];
static volatile uint8_t writeIdx = 0;
static volatile uint8_t readIdx  = 1;

// Counting semaphore (max=1): extra Give while task is busy drops silently.
static SemaphoreHandle_t frameReady  = nullptr;
static SemaphoreHandle_t serialMutex = nullptr;
static TaskHandle_t      streamTaskHandle = nullptr;

static const uint8_t MAGIC_IMAGE[4] = {0xAA, 0x55, 0xBB, 0x44};

void streamTask(void* pvParameters);

// logf — only emits in OUTPUT_LOG builds; silent in OUTPUT_STREAM
static void logf(const char* fmt, ...) {
#ifdef OUTPUT_LOG
    va_list args; va_start(args, fmt); Serial.vprintf(fmt, args); va_end(args);
#else
    (void)fmt;
#endif
}

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup() {
    esp_log_level_set("*", ESP_LOG_NONE);   // suppress HAL noise on USB serial

    Serial.begin(SERIAL_DEBUG_BAUD);
    Serial1.begin(SERIAL_TEENSY_BAUD, SERIAL_8N1, D7, D6);
    while (!Serial1) delay(10);

    logf("\n=== XIAO ESP32-S3 Vision System ===\n");

    if (!Camera_Init()) logf("Camera init failed!\n");
    else                logf("Camera ready\n");

    delay(500);

#ifdef OUTPUT_STREAM
    if (!psramFound()) {
        Serial.println("FATAL: PSRAM not found");
        while (true) delay(1000);
    }
    streamBuf[0] = (uint8_t*)ps_malloc(FRAME_BYTES);
    streamBuf[1] = (uint8_t*)ps_malloc(FRAME_BYTES);
    if (!streamBuf[0] || !streamBuf[1]) {
        Serial.println("FATAL: PSRAM alloc failed");
        while (true) delay(1000);
    }
    frameReady  = xSemaphoreCreateCounting(1, 0);
    serialMutex = xSemaphoreCreateMutex();
    if (!frameReady || !serialMutex) {
        Serial.println("FATAL: FreeRTOS primitives failed");
        while (true) delay(1000);
    }
    xTaskCreatePinnedToCore(streamTask, "streamTask", 4096, nullptr, 1, &streamTaskHandle, 0);
    logf("Stream task on Core 0\n");
#endif
}

// ── Main loop (Core 1) ─────────────────────────────────────────────────────────
void loop() {
    // ── Receive current mode from Teensy ──────────────────────────────────────
    teensy.update();
    uint8_t mode = teensy.get(XIAO_REG_MODE);   // default 0 if Teensy hasn't sent yet

    SPRINTF(SPRINT_SERIAL_IN, "[SIN]", "mode=%d", mode);

    // ── Grab frame ────────────────────────────────────────────────────────────
    camera_fb_t* fb = Camera_Grab();
    if (!fb) { logf("Frame grab failed\n"); return; }

    // ── Dispatch to active mode ───────────────────────────────────────────────
    switch (mode) {
        case MODE_LINEFOLLOW: modeLineFollowRun(fb, teensy); break;
        case MODE_EVAC:       modeEvacRun(fb, teensy);       break;
        case MODE_NOGI:       modeNoGIRun(fb, teensy);       break;
        case MODE_GAP:        modeGapRun(fb, teensy);        break;
        default:              modeLineFollowRun(fb, teensy); break;
    }

#ifdef OUTPUT_STREAM
    // ── Hand frame to stream task (non-blocking) ──────────────────────────────
    streamW[writeIdx] = (uint16_t)fb->width;
    streamH[writeIdx] = (uint16_t)fb->height;
    memcpy(streamBuf[writeIdx], fb->buf, fb->len);
    uint8_t next = readIdx; readIdx = writeIdx; writeIdx = next;
    xSemaphoreGive(frameReady);
#endif

    Camera_Return(fb);
}

// ── Stream task: Core 0 ────────────────────────────────────────────────────────
void streamTask(void* pvParameters) {
    static const int32_t ERR_ZERO = 0;
    const TickType_t minInterval = pdMS_TO_TICKS(1000 / STREAM_FPS);
    TickType_t lastSent = 0;

    for (;;) {
        if (xSemaphoreTake(frameReady, portMAX_DELAY) != pdTRUE) continue;

        // Rate-limit: drain any queued Give()s until the interval has passed,
        // then send only the latest frame — reduces USB interrupt pressure on Core 1.
        TickType_t now = xTaskGetTickCount();
        if ((now - lastSent) < minInterval) continue;
        lastSent = now;

        uint8_t  idx = readIdx;
        uint16_t w   = streamW[idx];
        uint16_t h   = streamH[idx];
        uint8_t* buf = streamBuf[idx];

        xSemaphoreTake(serialMutex, portMAX_DELAY);
        Serial.write(MAGIC_IMAGE,         4);
        Serial.write((uint8_t*)&w,        2);
        Serial.write((uint8_t*)&h,        2);
        Serial.write((uint8_t*)&ERR_ZERO, 4);
        Serial.write(buf,        FRAME_BYTES);
        // No Serial.flush() — driver sends async; we release mutex immediately.
        xSemaphoreGive(serialMutex);
    }
}
