#include "K230Decode.h"
#include "../sensors/K230_link.h"

#include <Arduino.h>

namespace Processing {
namespace K230Decode {

namespace {
    constexpr int16_t SENSOR_W = 640;
    constexpr int16_t SENSOR_H = 480;

    YacheK230D _k230d(Serial5);
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
}

void tick() {
    beginOnce();

    // Keep the existing sensor-layer command behavior intact so EVAC_Search /
    // EVAC_Exit can still control the K230 run state through setRunning().
    Sensors::K230_link::setCommand(_running ? K230D_CMD_RUN : K230D_CMD_IDLE);

    _k230d.update();
    if (_k230d.lastPacketMs() != 0 &&
        _k230d.lastPacketMs() != _last_published_packet_ms) {
        publishLegacyDetections();
        _last_published_packet_ms = _k230d.lastPacketMs();

#if PRINT_K230
        _k230d.printBoxes(Serial);
#endif
    }
}

const Detection* detections()   { return _detections; }
uint8_t          count()        { return _k230d.boxCount(); }
const Box*       boxes()        { return _k230d.boxCount() ? &_k230d.box(0) : nullptr; }
uint8_t          boxCount()     { return _k230d.boxCount(); }
uint32_t         lastPacketMs() { return _k230d.lastPacketMs(); }

void setRunning(bool run) { _running = run; }
bool isRunning()          { return _running; }

}  // namespace K230Decode
}  // namespace Processing
