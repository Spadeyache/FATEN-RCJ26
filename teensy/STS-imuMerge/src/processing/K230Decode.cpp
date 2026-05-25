#include "K230Decode.h"
#include "../sensors/K230_link.h"

namespace Processing {
namespace K230Decode {

namespace {
    enum ParseState : uint8_t { WAIT_HEADER, WAIT_COUNT, WAIT_DATA, WAIT_CHKSUM };

    ParseState _state = WAIT_HEADER;
    uint8_t    _count = 0;
    uint8_t    _idx   = 0;
    uint8_t    _buf[1 + K230_MAX_DETECTIONS * 3];

    Detection  _detections[K230_MAX_DETECTIONS];
    uint8_t    _detection_count = 0;
    bool       _running         = false;
}

void tick() {
    // 1. Push running flag down to the link layer so it sends the right command.
    Sensors::K230_link::setCommand(_running ? 0x01 : 0x00);

    // 2. Drain pending bytes through the frame parser.
    int b;
    while ((b = Sensors::K230_link::readByte()) >= 0) {
        const uint8_t byte = (uint8_t)b;

        switch (_state) {
            case WAIT_HEADER:
                if (byte == 0xAA) _state = WAIT_COUNT;
                break;

            case WAIT_COUNT:
                if (byte > K230_MAX_DETECTIONS) { _state = WAIT_HEADER; break; }
                _count  = byte;
                _buf[0] = byte;
                _idx    = 0;
                _state  = (_count == 0) ? WAIT_CHKSUM : WAIT_DATA;
                break;

            case WAIT_DATA:
                _buf[1 + _idx++] = byte;
                if (_idx >= (uint8_t)(_count * 3)) _state = WAIT_CHKSUM;
                break;

            case WAIT_CHKSUM: {
                uint8_t chk = 0;
                for (uint8_t i = 0; i <= _count * 3; i++) chk ^= _buf[i];
                if (chk == byte) {
                    _detection_count = _count;
                    for (uint8_t i = 0; i < _count; i++) {
                        _detections[i].type = (ObjectType)_buf[1 + i * 3];
                        _detections[i].x    =             _buf[2 + i * 3];
                        _detections[i].y    =             _buf[3 + i * 3];
                    }
                }
                _state = WAIT_HEADER;
                break;
            }
        }
    }
}

const Detection* detections() { return _detections; }
uint8_t          count()      { return _detection_count; }

void setRunning(bool run)     { _running = run; }
bool isRunning()              { return _running; }

}  // namespace K230Decode
}  // namespace Processing
