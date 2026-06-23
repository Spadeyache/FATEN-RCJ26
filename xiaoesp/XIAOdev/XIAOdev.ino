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
#include "src/stream/XiaoStream.h"
// #include "src/drivers/wifi_config.h"

// Stream FPS cap — limits USB interrupt pressure on Core 1.
// Lower = less impact on loop speed. 10 is a good balance.
#define STREAM_FPS 3

#if defined(OUTPUT_STREAM) == defined(OUTPUT_LOG)
  #error "Define exactly one of OUTPUT_STREAM or OUTPUT_LOG in config.h"
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
void runCameraCalibration();   // blocking RGB calibration mode (see bottom of file)

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
    // Serial.begin(115200);
    // while(true){Serial.println("start");}
    esp_log_level_set("*", ESP_LOG_NONE);   // suppress HAL noise on USB serial

    Serial.begin(SERIAL_DEBUG_BAUD);
    Serial1.begin(SERIAL_TEENSY_BAUD, SERIAL_8N1, D7, D6);
    while (!Serial1) delay(10);

    logf("\n=== XIAO ESP32-S3 Vision System ===\n");

    if (!Camera_Init()) logf("Camera init failed!\n");
    else                logf("Camera ready\n");

    delay(500);

    // ── CAMERA RGB CALIBRATION ──────────────────────────────────────────────
    //  Uncomment to enter calibration mode (blocks forever). Streams frames to
    //  esp32_camera_viewer.html and reads SPACE from the viewer: aim BLACK +
    //  SPACE, then WHITE + SPACE → it prints a paste-ready vision.cpp block.
    //  Paste the 6 constants into vision.cpp, then comment this back out.
    // runCameraCalibration();

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
    // ── Receive current mode from Teensy ──────────────────────────────────────
    teensy.update();
    uint8_t mode = teensy.get(XIAO_REG_MODE);   // default 0 if Teensy hasn't sent yet
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
}

#ifdef OUTPUT_STREAM
static void sendStreamDebugOnly() {
    static uint32_t lastSent = 0;
    const uint32_t now = millis();
    if ((uint32_t)(now - lastSent) < (1000UL / STREAM_FPS)) return;
    lastSent = now;

    char lcLine[384];
    char rowLine[48];
    char evtLine[96];
    int lcLen = xs_formatLineDebug(lcLine, sizeof(lcLine));
    int rowLen = xs_formatSensorRow(rowLine, sizeof(rowLine));

    if (serialMutex) xSemaphoreTake(serialMutex, portMAX_DELAY);
    int evtLen = 0;
    while ((evtLen = xs_formatEvent(evtLine, sizeof(evtLine))) > 0) {
        Serial.write((const uint8_t*)evtLine, evtLen);
    }
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
        char rowLine[48];
        char evtLine[96];
        int  lcLen = xs_formatLineDebug(lcLine, sizeof(lcLine));
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

// ════════════════════════════════════════════════════════════════════════════════
//  CAMERA RGB CALIBRATION (blocking)
//  Streams frames over the normal protocol and reads RAW RGB averaged over a
//  centre box, smoothed across CALIB_AVG_FRAMES frames. The viewer sends SPACE
//  (no newline) on a key press: 1st SPACE locks BLACK, 2nd locks WHITE; 'r'
//  resets. On both → prints a paste-ready vision.cpp block as [CAL] lines.
//  Runs single-threaded from setup() before the stream task exists → no mutex.
// ════════════════════════════════════════════════════════════════════════════════
static void calSampleBox(camera_fb_t* fb, float& mr, float& mg, float& mb) {
    const int w = fb->width, h = fb->height;
    long sr = 0, sg = 0, sb = 0; int n = 0;
    for (int y = CALIB_BOX_CY - CALIB_BOX_HALF; y <= CALIB_BOX_CY + CALIB_BOX_HALF; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = CALIB_BOX_CX - CALIB_BOX_HALF; x <= CALIB_BOX_CX + CALIB_BOX_HALF; x++) {
            if (x < 0 || x >= w) continue;
            uint16_t px = unpackRGB565(fb->buf, y * w + x);
            uint8_t r, g, b; rgb565To888(px, r, g, b);
            sr += r; sg += g; sb += b; n++;
        }
    }
    if (n == 0) { mr = mg = mb = 0; return; }
    mr = (float)sr / n; mg = (float)sg / n; mb = (float)sb / n;
}

#if defined(OUTPUT_STREAM) && STREAM_SEND_CAMERA_IMAGES
static void calStreamFrame(camera_fb_t* fb) {
    const uint16_t w = fb->width, h = fb->height;
    const uint32_t plen = (uint32_t)w * (uint32_t)h * 2UL;
    const uint16_t crc  = fletcher16(fb->buf, plen);
    Serial.write(MAGIC_IMAGE,     4);
    Serial.write((uint8_t*)&w,    2);
    Serial.write((uint8_t*)&h,    2);
    Serial.write((uint8_t*)&plen, 4);
    Serial.write(fb->buf,         plen);
    Serial.write((uint8_t*)&crc,  2);
}
#endif

void runCameraCalibration() {
    const int N = CALIB_AVG_FRAMES;
    float rBuf[N], gBuf[N], bBuf[N];
    int   count = 0, head = 0;
    float blkR = 0, blkG = 0, blkB = 0; bool haveBlk = false;
    float whtR = 0, whtG = 0, whtB = 0; bool haveWht = false;
    uint32_t lastLive = 0;

    Serial.printf("[CAL] calibration mode. Aim BLACK + SPACE, then WHITE + SPACE. 'r' = reset.\n");

    for (;;) {
        camera_fb_t* fb = Camera_Grab();
        if (!fb) { delay(5); continue; }

        float mr, mg, mb;
        calSampleBox(fb, mr, mg, mb);
        rBuf[head] = mr; gBuf[head] = mg; bBuf[head] = mb;
        head = (head + 1) % N;
        if (count < N) count++;

        float ar = 0, ag = 0, ab = 0;
        for (int i = 0; i < count; i++) { ar += rBuf[i]; ag += gBuf[i]; ab += bBuf[i]; }
        ar /= count; ag /= count; ab /= count;

        // ── keypresses relayed from the viewer (single chars, no newline) ──
        while (Serial.available()) {
            char c = Serial.read();
            if (c == 'r' || c == 'R') {
                haveBlk = haveWht = false;
                Serial.printf("[CAL] reset. Aim BLACK + SPACE.\n");
            } else if (c == ' ' || c == 'b' || c == 'B' || c == 'w' || c == 'W') {
                if (count < N) {
                    Serial.printf("[CAL] not enough data (%d/%d)\n", count, N);
                    continue;
                }
                bool asWhite = (c == 'w' || c == 'W') || (c == ' ' && haveBlk);
                if (!asWhite) {
                    blkR = ar; blkG = ag; blkB = ab; haveBlk = true;
                    Serial.printf("[CAL] BLACK locked R=%.1f G=%.1f B=%.1f -> aim WHITE + SPACE\n", ar, ag, ab);
                } else {
                    whtR = ar; whtG = ag; whtB = ab; haveWht = true;
                    Serial.printf("[CAL] WHITE locked R=%.1f G=%.1f B=%.1f\n", ar, ag, ab);
                }
                if (haveBlk && haveWht) {
                    float gR = 255.0f / fmaxf(1.0f, whtR - blkR);
                    float gG = 255.0f / fmaxf(1.0f, whtG - blkG);
                    float gB = 255.0f / fmaxf(1.0f, whtB - blkB);
                    Serial.printf("[CAL] ===== PASTE INTO vision.cpp =====\n");
                    Serial.printf("[CAL] const float R_Gain = %.8f;   // 255/(%.1f-%.1f)\n", gR, whtR, blkR);
                    Serial.printf("[CAL] const float G_Gain = %.8f;   // 255/(%.1f-%.1f)\n", gG, whtG, blkG);
                    Serial.printf("[CAL] const float B_Gain = %.8f;   // 255/(%.1f-%.1f)\n", gB, whtB, blkB);
                    Serial.printf("[CAL] const uint8_t R_D = %.1f * %.2f;   // (=%d)\n", blkR, (double)CALIB_MARGIN, (int)(blkR * CALIB_MARGIN));
                    Serial.printf("[CAL] const uint8_t G_D = %.1f * %.2f;   // (=%d)\n", blkG, (double)CALIB_MARGIN, (int)(blkG * CALIB_MARGIN));
                    Serial.printf("[CAL] const uint8_t B_D = %.1f * %.2f;   // (=%d)\n", blkB, (double)CALIB_MARGIN, (int)(blkB * CALIB_MARGIN));
                    Serial.printf("[CAL] =================================\n");
                    haveBlk = haveWht = false;
                }
            }
        }

        // ── throttled live readout + frame stream ──
        if (millis() - lastLive >= 250) {
            lastLive = millis();
            if (count < N) {
                Serial.printf("[CAL] live R=%.1f G=%.1f B=%.1f  (warming %d/%d)\n", ar, ag, ab, count, N);
            } else {
                Serial.printf("[CAL] live R=%.1f G=%.1f B=%.1f  n=%d  %s + SPACE\n",
                              ar, ag, ab, count, haveBlk ? "aim WHITE" : "aim BLACK");
            }
#if defined(OUTPUT_STREAM) && STREAM_SEND_CAMERA_IMAGES
            calStreamFrame(fb);
#endif
        }
        Camera_Return(fb);
    }
}
