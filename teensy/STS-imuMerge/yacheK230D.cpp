// yacheK230D.cpp -- variable-length box parser. See yacheK230D.h.

#include "yacheK230D.h"

static constexpr uint8_t SYNC0 = 0xAA;
static constexpr uint8_t SYNC1 = 0x55;
static constexpr uint8_t BOX_BYTES = 10;

YacheK230D::YacheK230D(HardwareSerial &serial) : _serial(serial) {}

void YacheK230D::begin(uint32_t baud) {
    _serial.begin(baud);
    _state = WAIT_AA;
}

void YacheK230D::sendCommand(K230DCommand cmd) {
    _serial.write(static_cast<uint8_t>(cmd));
}

int YacheK230D::update() {
    int frameFinished = 0;
    while (_serial.available()) {
        uint8_t b = static_cast<uint8_t>(_serial.read());
        switch (_state) {
            case WAIT_AA:
                if (b == SYNC0) {
                    _state    = WAIT_55;
                    _checksum = b;
                }
                break;

            case WAIT_55:
                if (b == SYNC1) {
                    _state    = READ_COUNT;
                    _checksum ^= b;
                } else {
                    _state = WAIT_AA;
                }
                break;

            case READ_COUNT:
                _expectedCount = b;
                _checksum     ^= b;
                _boxesRecvd    = 0;
                _byteIdx       = 0;
                if (_expectedCount == 0) {
                    _state = READ_CHECKSUM;
                } else if (_expectedCount > K230D_MAX_BOXES_RX) {
                    Serial.print("K230D RX bad count=");
                    Serial.println(_expectedCount);
                    // Suspicious -- reset, don't even try.
                    _state = WAIT_AA;
                } else {
                    Serial.print("K230D RX frame count=");
                    Serial.println(_expectedCount);
                    _state = READ_BOXES;
                }
                break;

            case READ_BOXES:
                _checksum ^= b;
                switch (_byteIdx) {
                    case 0: _parsing.cls   = b; break;
                    case 1: _parsing.score = b; break;
                    case 2: _parsing.x1 = static_cast<int16_t>(static_cast<uint16_t>(b) << 8); break;
                    case 3: _parsing.x1 = static_cast<int16_t>(static_cast<uint16_t>(_parsing.x1) | b); break;
                    case 4: _parsing.y1 = static_cast<int16_t>(static_cast<uint16_t>(b) << 8); break;
                    case 5: _parsing.y1 = static_cast<int16_t>(static_cast<uint16_t>(_parsing.y1) | b); break;
                    case 6: _parsing.x2 = static_cast<int16_t>(static_cast<uint16_t>(b) << 8); break;
                    case 7: _parsing.x2 = static_cast<int16_t>(static_cast<uint16_t>(_parsing.x2) | b); break;
                    case 8: _parsing.y2 = static_cast<int16_t>(static_cast<uint16_t>(b) << 8); break;
                    case 9:
                        _parsing.y2 = static_cast<int16_t>(static_cast<uint16_t>(_parsing.y2) | b);
                        if (_boxesRecvd < K230D_MAX_BOXES_RX) {
                            _boxes[_boxesRecvd] = _parsing;
                            Serial.print("K230D RX BOX raw cls=");
                            Serial.print(_parsing.cls);
                            Serial.print(" score=");
                            Serial.print(_parsing.score);
                            Serial.print(" x1=");
                            Serial.print(_parsing.x1);
                            Serial.print(" y1=");
                            Serial.print(_parsing.y1);
                            Serial.print(" x2=");
                            Serial.print(_parsing.x2);
                            Serial.print(" y2=");
                            Serial.println(_parsing.y2);
                        }
                        _boxesRecvd++;
                        _byteIdx = 0;
                        if (_boxesRecvd >= _expectedCount) {
                            _state = READ_CHECKSUM;
                            continue;
                        }
                        continue;
                }
                _byteIdx++;
                break;

            case READ_CHECKSUM:
                if (b == _checksum) {
                    _boxCount = (_expectedCount > K230D_MAX_BOXES_RX)
                                ? K230D_MAX_BOXES_RX
                                : _expectedCount;
                    _lastPacketMs = millis();
                    frameFinished += _boxCount;
                } else {
                    Serial.print("K230D RX checksum fail got=");
                    Serial.print(b);
                    Serial.print(" expected=");
                    Serial.println(_checksum);
                    frameFinished = -1;
                }
                _state = WAIT_AA;
                break;
        }
    }
    return frameFinished;
}

void YacheK230D::printBoxes(Stream &out) const {
    out.print("K230D BOXES n=");
    out.println(_boxCount);
    for (uint8_t i = 0; i < _boxCount; i++) {
        const K230DBox &b = _boxes[i];
        out.print("K230D BOX cls=");
        out.print(b.cls);
        out.print(" score=");
        out.print(b.score);
        out.print(" x1=");
        out.print(b.x1);
        out.print(" y1=");
        out.print(b.y1);
        out.print(" x2=");
        out.print(b.x2);
        out.print(" y2=");
        out.println(b.y2);
    }
}
