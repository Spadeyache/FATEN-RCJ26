#include "K230Decode.h"
#include "../sensors/K230_link.h"
#include "../../pins_teensy.h"

#include <Arduino.h>

namespace Processing {
namespace K230Decode {

namespace goalPOS {
bool valid = false;
float direction = 0.0f;
uint8_t cls = 0;
uint8_t score = 0;
uint32_t updatedMs = 0;
}

namespace pointPOS {
bool valid = false;
float direction = 0.0f;
uint8_t score = 0;
uint32_t updatedMs = 0;
}

namespace {
    constexpr int16_t SENSOR_W = (int16_t)K230_FRAME_WIDTH;
    constexpr int16_t SENSOR_H = 480;

    YacheK230D _k230d(K230_SERIAL);
    Detection _detections[K230D_MAX_BOXES_RX];
    bool      _running = false;
    bool      _begun = false;
    uint32_t  _last_published_packet_ms = 0;

    uint8_t clampU8(int32_t v) {
        if (v < 0) return 0;
        if (v > 255) return 255;
        return (uint8_t)v;
    }

    void beginOnce() {
        if (_begun) return;
        _k230d.begin(K230_BAUD);
        _begun = true;
    }

    void publishLegacyDetections() {
        for (uint8_t i = 0; i < _k230d.boxCount(); i++) {
            const K230DBox &b = _k230d.box(i);
            const int32_t cx = ((int32_t)b.x1 + (int32_t)b.x2) / 2;
            const int32_t cy = ((int32_t)b.y1 + (int32_t)b.y2) / 2;

            _detections[i].type = (ObjectType)b.cls;
            _detections[i].x = clampU8((cx * 255L) / SENSOR_W);
            _detections[i].y = clampU8((cy * 255L) / SENSOR_H);
        }
    }

    float normalizedDirectionFor(const K230DBox &b) {
        const float center_x = ((float)b.x1 + (float)b.x2) * 0.5f;
        float direction = (center_x - K230_FRAME_CENTER_X) / K230_FRAME_CENTER_X;
        if (direction < -1.0f) direction = -1.0f;
        if (direction >  1.0f) direction =  1.0f;
        return direction;
    }

    // Map raw model class -> viewer numbering (0=alive/silver, 1=dead/black,
    // 2=evac point). Keeps the historic ball swap so the box-viewer HTML colours
    // stay consistent, and passes the point class through as 2.
    uint8_t viewerClassFor(uint8_t rawCls) {
        if (rawCls == K230_CLASS_ALIVE) return 0;
        if (rawCls == K230_CLASS_DEAD)  return 1;
        if (rawCls == K230_CLASS_POINT) return 2;
        return rawCls;
    }

    void printViewerBoxes() {
        Serial.print(F("K230D BOXES n="));
        Serial.println(_k230d.boxCount());
        for (uint8_t i = 0; i < _k230d.boxCount(); i++) {
            const K230DBox &b = _k230d.box(i);
            Serial.print(F("K230D BOX cls="));
            Serial.print(viewerClassFor(b.cls));
            Serial.print(F(" score="));
            Serial.print(b.score);
            Serial.print(F(" x1="));
            Serial.print(b.x1);
            Serial.print(F(" y1="));
            Serial.print(b.y1);
            Serial.print(F(" x2="));
            Serial.print(b.x2);
            Serial.print(F(" y2="));
            Serial.println(b.y2);
        }
    }
}

void tick() {
    beginOnce();

    // Teensy-side K230 run/idle control disabled for now.
    // Sensors::K230_link::setCommand(_running ? K230D_CMD_RUN : K230D_CMD_IDLE);

    _k230d.update();
    if (_k230d.lastPacketMs() != 0 &&
        _k230d.lastPacketMs() != _last_published_packet_ms) {
        publishLegacyDetections();
        _last_published_packet_ms = _k230d.lastPacketMs();

#if PRINT_K230
        printViewerBoxes();
#endif
    }
}

const Detection* detections()   { return _detections; }
uint8_t          count()        { return _k230d.boxCount(); }
const Box*       boxes()        { return _k230d.boxCount() ? &_k230d.box(0) : nullptr; }
uint8_t          boxCount()     { return _k230d.boxCount(); }
uint32_t         lastPacketMs() { return _k230d.lastPacketMs(); }

bool isVictimClass(uint8_t cls) {
    return cls == K230_CLASS_DEAD || cls == K230_CLASS_ALIVE;
}

bool isPointClass(uint8_t cls) {
    return cls == K230_CLASS_POINT;
}

bool updateGoalFromVictim(const K230DBox &msg) {
    if (!isVictimClass(msg.cls)) return false;

    goalPOS::valid = true;
    goalPOS::direction = normalizedDirectionFor(msg);
    goalPOS::cls = msg.cls;
    goalPOS::score = msg.score;
    goalPOS::updatedMs = millis();
    return true;
}

// Check one decoded K230D box and update goalPOS when it is silver/black.
bool checkVictim(const K230DBox &msg) {
    return updateGoalFromVictim(msg);
}

// Check an explicit list of decoded K230D boxes. The highest-score silver/black
// box becomes the current goalPOS target.
bool checkVictim(const K230DBox *msgs, uint8_t msgCount) {
    if (msgs == nullptr || msgCount == 0) {
        goalPOS::valid = false;
        return false;
    }

    const K230DBox *best = nullptr;
    for (uint8_t i = 0; i < msgCount; i++) {
        if (!isVictimClass(msgs[i].cls)) continue;
        if (best == nullptr || msgs[i].score > best->score) {
            best = &msgs[i];
        }
    }

    if (best == nullptr) {
        goalPOS::valid = false;
        return false;
    }
    return updateGoalFromVictim(*best);
}

// Check the latest K230D frame held by this decoder.
bool checkVictim() {
    return checkVictim(boxes(), boxCount());
}

// Pick the highest-score evac-point box in an explicit list and load pointPOS.
bool checkPoint(const K230DBox *msgs, uint8_t msgCount) {
    if (msgs == nullptr || msgCount == 0) {
        pointPOS::valid = false;
        return false;
    }

    const K230DBox *best = nullptr;
    for (uint8_t i = 0; i < msgCount; i++) {
        if (!isPointClass(msgs[i].cls)) continue;
        if (best == nullptr || msgs[i].score > best->score) {
            best = &msgs[i];
        }
    }

    if (best == nullptr) {
        pointPOS::valid = false;
        return false;
    }

    pointPOS::valid     = true;
    pointPOS::direction = normalizedDirectionFor(*best);
    pointPOS::score     = best->score;
    pointPOS::updatedMs = millis();
    return true;
}

// Check the latest K230D frame held by this decoder for an evac point.
bool checkPoint() {
    return checkPoint(boxes(), boxCount());
}

// Returns the center X pixel (0..639) of the largest (by box area) detection
// of the given class in the most recent frame. Returns -1 if none found.
int16_t largestCenterX(uint8_t cls) {
    const K230DBox *b     = boxes();
    const uint8_t   n     = boxCount();
    const K230DBox *best  = nullptr;
    int32_t         bestArea = -1;

    for (uint8_t i = 0; i < n; i++) {
        if (b[i].cls != cls) continue;
        const int32_t area = (int32_t)(b[i].x2 - b[i].x1) * (int32_t)(b[i].y2 - b[i].y1);
        if (area > bestArea) {
            bestArea = area;
            best = &b[i];
        }
    }

    if (best == nullptr) return -1;
    return (int16_t)(((int32_t)best->x1 + (int32_t)best->x2) / 2);
}

void setRunning(bool run) {
    if (_running == run) return;
    _running = run;

    // Teensy-side K230 run/idle control disabled for now.
    // const K230DCommand cmd = _running ? K230D_CMD_RUN : K230D_CMD_IDLE;
    // beginOnce();
    // Sensors::K230_link::setCommand(cmd);
    // _k230d.sendCommand(cmd);

#if PRINT_K230
    Serial.print(F("K230D CMD "));
    Serial.println(_running ? F("RUN") : F("IDLE"));
#endif
}
bool isRunning()          { return _running; }

}  // namespace K230Decode
}  // namespace Processing
