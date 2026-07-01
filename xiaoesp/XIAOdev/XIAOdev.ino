#include <Arduino.h>
#include "esp_log.h"
#include "src/drivers/yacheEncodedSerial.h"
#include "src/drivers/camera_config.h"
#include "src/processing/vision.h"
#include "src/config/config.h"
#include "src/config/serial_print.h"
#include "src/modes/ModeLineFollow.h"
#include "src/modes/ModeSearchLine.h"
#include "src/modes/ModeNoGI.h"
#include "src/modes/ModeLineAngle.h"
#include "src/modes/ModeObstacle.h"
#include "src/modes/ModeEvacColorMask.h"
#include "src/stream/XiaoStream.h"
// #include "src/drivers/wifi_config.h"

// Stream FPS cap — limits USB interrupt pressure on Core 1.
// Lower = less impact on loop speed. 10 is a good balance.
#define STREAM_FPS 3
#define CAL_CENTER_SAMPLE_SIZE   10

#if (defined(OUTPUT_STREAM) + defined(OUTPUT_LOG) + defined(OUTPUT_CALIBRATE)) != 1
  #error "Define exactly one of OUTPUT_STREAM, OUTPUT_LOG, or OUTPUT_CALIBRATE in config.h"
#endif

// ── Serial link to Teensy ──────────────────────────────────────────────────────
YacheEncodedSerial teensy(Serial1);

// ── Dual-core streaming globals ────────────────────────────────────────────────
static constexpr size_t FRAME_BYTES = CAMERA_FRAME_BYTES;

static uint8_t*  streamBuf[2]    = {nullptr, nullptr};
static uint16_t  streamW[2], streamH[2];
static volatile uint8_t writeIdx = 0;
static volatile uint8_t readIdx  = 1;

// Counting semaphore (max=1): extra Give while task is busy drops silently.
static SemaphoreHandle_t frameReady  = nullptr;
static SemaphoreHandle_t serialMutex = nullptr;
static TaskHandle_t      streamTaskHandle = nullptr;

static const uint8_t MAGIC_IMAGE[4] = {0xAA, 0x55, 0xBB, 0x44};

// Fletcher-16 over the pixel payload. Cheap, and any dropped/duplicated byte
// shifts the running sums so the viewer's recomputed value won't match → the
// torn frame is discarded instead of rendered as garbage.
static uint16_t fletcher16(const uint8_t* d, size_t n) {
    uint16_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) { s1 = (s1 + d[i]) % 255; s2 = (s2 + s1) % 255; }
    return (uint16_t)((s2 << 8) | s1);
}

void streamTask(void* pvParameters);
static void sendStreamDebugOnly();
static bool sampleCenterRawRgb(camera_fb_t* fb, uint8_t& r, uint8_t& g, uint8_t& b, uint16_t& n);

// logf — only emits in OUTPUT_LOG builds; silent in OUTPUT_STREAM
static void logf(const char* fmt, ...) {
#if defined(OUTPUT_LOG)
    va_list args; va_start(args, fmt); Serial.vprintf(fmt, args); va_end(args);
#else
    (void)fmt;
#endif
}

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup() {
    // Serial.begin(115200);
    // while(true){Serial.println("start");}
    esp_log_level_set("*", ESP_LOG_NONE);   // suppress HAL noise on USB serial

    Serial.begin(SERIAL_DEBUG_BAUD);
#ifndef OUTPUT_CALIBRATE
    Serial1.begin(SERIAL_TEENSY_BAUD, SERIAL_8N1, SERIAL_TEENSY_RX_PIN, SERIAL_TEENSY_TX_PIN);
    while (!Serial1) delay(10);
#endif

    logf("\n=== XIAO ESP32-S3 Vision System ===\n");

    if (!Camera_Init()) logf("Camera init failed!\n");
    else                logf("Camera ready\n");

    delay(500);

#ifdef OUTPUT_STREAM
#if STREAM_SEND_CAMERA_IMAGES
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
#else
    serialMutex = xSemaphoreCreateMutex();
    if (!serialMutex) {
        Serial.println("FATAL: FreeRTOS primitives failed");
        while (true) delay(1000);
    }
    logf("Stream image payload disabled; sending [LC]/[ROW] debug only\n");
#endif
#endif
    pinMode(LED_BUILTIN, OUTPUT);
}

// ── Main loop (Core 1) ─────────────────────────────────────────────────────────
void loop() {
#ifdef OUTPUT_CALIBRATE
    camera_fb_t* fb = Camera_Grab();
    if (!fb) {
        Serial.println("[CAL] error=frame_grab_failed");
        delay(50);
        return;
    }

    uint8_t r = 0, g = 0, b = 0;
    uint16_t n = 0;
    if (sampleCenterRawRgb(fb, r, g, b, n)) {
        Serial.printf("[CAL] r=%u g=%u b=%u n=%u\n", r, g, b, n);
    } else {
        Serial.println("[CAL] error=sample_failed");
    }
    Camera_Return(fb);
    delay(50);
    return;
#else

    // ── Receive current mode from Teensy ──────────────────────────────────────
    teensy.update();
#if DEBUG_FORCE_LINE_ANGLE_MODE
    // Debug only: comment out the Teensy/state-machine mode and force angle mode.
    // uint8_t mode = teensy.get(XIAO_REG_MODE);
    uint8_t mode = MODE_LINE_ANGLE;
#elif DEBUG_FORCE_EVAC_COLOR_MASK_MODE
    // Debug only: force evacuation color mask mode.
    uint8_t mode = MODE_EVAC_COLOR_MASK;
#else
    uint8_t mode = teensy.get(XIAO_REG_MODE);   // default 0 if Teensy hasn't sent yet
#endif
    static uint8_t s_prevMode = 255;
    if (mode != s_prevMode) {
        if (mode == MODE_LINEFOLLOW) modeLineFollowReset();
        s_prevMode = mode;
    }

    SPRINTF(SPRINT_SERIAL_IN, "[SIN]", "mode=%d", mode);

    // ── Grab frame ────────────────────────────────────────────────────────────
    camera_fb_t* fb = Camera_Grab();
    if (!fb) { logf("Frame grab failed\n"); return; }

    // ── Dispatch to active mode ───────────────────────────────────────────────
    //  Line-follow mode 0 uses modeLineFollowRun(), currently the arc-ROI
    //  black-line steering path.
    switch (mode) {
        case MODE_LINEFOLLOW:  modeLineFollowRun(fb, teensy); break;
        case MODE_SEARCH_LINE: modeSearchLineRun(fb, teensy);  break;
        case MODE_NOGI:        modeNoGIRun(fb, teensy);        break;
        case MODE_LINE_ANGLE:  modeLineAngleRun(fb, teensy);   break;
        case MODE_OBSTACLE:    modeObstacleRun(fb, teensy);    break;
        case MODE_EVAC_COLOR_MASK: modeEvacColorMaskRun(fb, teensy); break;
        default:               modeLineFollowRun(fb, teensy);  break;
    }

#ifdef OUTPUT_STREAM
#if STREAM_SEND_CAMERA_IMAGES
    // ── Hand frame to stream task (non-blocking) ──────────────────────────────
    streamW[writeIdx] = (uint16_t)fb->width;
    streamH[writeIdx] = (uint16_t)fb->height;
    memcpy(streamBuf[writeIdx], fb->buf, fb->len);
    uint8_t next = readIdx; readIdx = writeIdx; writeIdx = next;
    xSemaphoreGive(frameReady);
#else
    sendStreamDebugOnly();
#endif
#endif

    Camera_Return(fb);
#endif
}

#ifdef OUTPUT_STREAM
static void sendStreamDebugOnly() {
    static uint32_t lastSent = 0;
    const uint32_t now = millis();
    if ((uint32_t)(now - lastSent) < (1000UL / STREAM_FPS)) return;
    lastSent = now;

    char lcLine[384];
    char laLine[256];
    char saLine[1024];
    char rowLine[128];
    char evtLine[96];
    int lcLen = xs_formatLineDebug(lcLine, sizeof(lcLine));
    int laLen = xs_formatLineAngleDebug(laLine, sizeof(laLine));
    int saLen = xs_formatEvacTapeDebug(saLine, sizeof(saLine));
    int rowLen = xs_formatSensorRow(rowLine, sizeof(rowLine));

    if (serialMutex) xSemaphoreTake(serialMutex, portMAX_DELAY);
    int evtLen = 0;
    while ((evtLen = xs_formatEvent(evtLine, sizeof(evtLine))) > 0) {
        Serial.write((const uint8_t*)evtLine, evtLen);
    }
    if (laLen > 0) Serial.write((const uint8_t*)laLine, laLen);
    if (saLen > 0) Serial.write((const uint8_t*)saLine, saLen);
    if (lcLen > 0) Serial.write((const uint8_t*)lcLine, lcLen);
    if (rowLen > 0) Serial.write((const uint8_t*)rowLine, rowLen);
    if (serialMutex) xSemaphoreGive(serialMutex);
}
#endif

#if defined(OUTPUT_STREAM) && STREAM_SEND_CAMERA_IMAGES
// ── Stream task: Core 0 ────────────────────────────────────────────────────────
void streamTask(void* pvParameters) {
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

        // LineCount overlay line — ASCII, emitted under the mutex *before* the
        // frame so the viewer parses it as text (never inside the pixel bytes).
        char lcLine[384];
        char laLine[256];
        char saLine[1024];
        char rowLine[128];
        char evtLine[96];
        int  lcLen = xs_formatLineDebug(lcLine, sizeof(lcLine));
        int  laLen = xs_formatLineAngleDebug(laLine, sizeof(laLine));
        int  saLen = xs_formatEvacTapeDebug(saLine, sizeof(saLine));
        int  rowLen = xs_formatSensorRow(rowLine, sizeof(rowLine));

        // Frame integrity: send payload length + a Fletcher-16 checksum so the
        // viewer can drop torn frames and resync instead of rendering garbage.
        const uint32_t plen = (uint32_t)w * (uint32_t)h * 2UL;
        const uint16_t crc  = fletcher16(buf, plen);

        xSemaphoreTake(serialMutex, portMAX_DELAY);
        int evtLen = 0;
        while ((evtLen = xs_formatEvent(evtLine, sizeof(evtLine))) > 0) {
            Serial.write((const uint8_t*)evtLine, evtLen);
        }
        if (laLen > 0) Serial.write((const uint8_t*)laLine, laLen);
        if (saLen > 0) Serial.write((const uint8_t*)saLine, saLen);
        if (lcLen > 0) Serial.write((const uint8_t*)lcLine, lcLen);
        if (rowLen > 0) Serial.write((const uint8_t*)rowLine, rowLen);
        Serial.write(MAGIC_IMAGE,      4);
        Serial.write((uint8_t*)&w,     2);
        Serial.write((uint8_t*)&h,     2);
        Serial.write((uint8_t*)&plen,  4);   // payload length (replaces old err field)
        Serial.write(buf,     FRAME_BYTES);
        Serial.write((uint8_t*)&crc,   2);   // checksum over the payload
        // No Serial.flush() — driver sends async; we release mutex immediately.
        xSemaphoreGive(serialMutex);
    }
}
#endif

static bool sampleCenterRawRgb(camera_fb_t* fb, uint8_t& r, uint8_t& g, uint8_t& b, uint16_t& n) {
    r = 0; g = 0; b = 0; n = 0;
    if (!fb || !fb->buf || fb->width == 0 || fb->height == 0) return false;

    const int size = CAL_CENTER_SAMPLE_SIZE;
    const int x0 = ((int)fb->width  - size) / 2;
    const int y0 = ((int)fb->height - size) / 2;
    if (x0 < 0 || y0 < 0) return false;

    uint32_t rSum = 0, gSum = 0, bSum = 0;
    for (int y = y0; y < y0 + size; y++) {
        for (int x = x0; x < x0 + size; x++) {
            uint8_t rr, gg, bb;
            rgb565To888(unpackRGB565(fb->buf, (size_t)y * fb->width + x), rr, gg, bb);
            rSum += rr;
            gSum += gg;
            bSum += bb;
            n++;
        }
    }

    if (n == 0) return false;
    r = (uint8_t)(rSum / n);
    g = (uint8_t)(gSum / n);
    b = (uint8_t)(bSum / n);
    return true;
}
