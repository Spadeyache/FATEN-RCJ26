// =============================================================================
//  yacheK230D.h
//  K230D Zero <-> Teensy serial link, variable-length box list protocol.
//
//  Wire format (K230 -> Teensy):
//
//    [0xAA] [0x55] [N] [box]xN [xor_checksum]
//
//  where each box is 10 bytes:
//
//    [cls] [score] [x1_hi] [x1_lo] [y1_hi] [y1_lo] [x2_hi] [x2_lo] [y2_hi] [y2_lo]
//
//      cls    : raw model class. 3-class model: 0=dead/black ball,
//               1=alive/silver ball, 2=evac point/corner. Caller maps semantics.
//      score  : 0..255  (= round(model_confidence * 255))
//      x1..y2 : signed 16-bit BE pixel coords in SENSOR frame (640x480)
//
//    checksum = XOR of every byte from 0xAA through the last box byte
//
//  Teensy -> K230:  single byte
//      0x00 = idle, 0x01 = run
//
//  Usage:
//      YacheK230D k230d(Serial5);
//      k230d.begin(115200);
//      ...
//      k230d.sendCommand(K230D_CMD_RUN);    // ask K230 to run
//      k230d.update();                       // call every loop iter -- non-blocking
//      if (k230d.hasFreshFrame(500)) {
//          for (uint8_t i = 0; i < k230d.boxCount(); i++) {
//              const K230DBox &b = k230d.box(i);
//              ...
//          }
//      }
// =============================================================================
#pragma once

#include <Arduino.h>

#define K230D_MAX_BOXES_RX     16

enum K230DCommand : uint8_t {
    K230D_CMD_IDLE          = 0x00,
    K230D_CMD_RUN           = 0x01,
    K230D_CMD_MODEL_VICTIMS = 0x02,   // K230: load victims.kmodel (Dead/Live/Point)
    K230D_CMD_MODEL_POINTS  = 0x03,   // K230: load points.kmodel  (corner colour)
};

enum K230DClass : uint8_t {
    K230D_CLS_DEAD  = 0,   // black ball
    K230D_CLS_ALIVE = 1,   // silver ball
    K230D_CLS_POINT = 2,   // evacuation point / corner
};

struct K230DBox {
    uint8_t cls;          // K230DClass (raw model class)
    uint8_t score;        // 0..255
    int16_t x1, y1, x2, y2;  // sensor-frame pixel coords
};

class YacheK230D {
public:
    explicit YacheK230D(HardwareSerial &serial);

    void begin(uint32_t baud = 115200);

    // Send single-byte run/idle command to the K230.
    void sendCommand(K230DCommand cmd);

    // Drain available bytes, advance parser. Returns the number of NEW
    // boxes received in this call (0 if no complete packet finished),
    // or -1 if a checksum mismatch was detected on a finished frame.
    // Safe to call every loop iteration.
    int update();

    // Last fully-received frame.
    uint8_t         boxCount() const         { return _boxCount; }
    const K230DBox &box(uint8_t i) const     { return _boxes[i]; }
    uint32_t        lastPacketMs() const     { return _lastPacketMs; }

    // True if we received a complete frame more recently than `maxAgeMs`.
    bool hasFreshFrame(uint32_t maxAgeMs) const {
        return _lastPacketMs != 0 &&
               (millis() - _lastPacketMs) <= maxAgeMs;
    }

    // Print every current box one-per-line on `out` in a format the
    // box-viewer HTML parses:
    //
    //   K230D BOXES n=2
    //   K230D BOX cls=0 score=204 x1=150 y1=200 x2=300 y2=350
    //   K230D BOX cls=1 score=165 x1=400 y1=100 x2=500 y2=180
    //
    void printBoxes(Stream &out) const;

private:
    HardwareSerial &_serial;

    enum ParseState : uint8_t {
        WAIT_AA, WAIT_55, READ_COUNT, READ_BOXES, READ_CHECKSUM
    };
    ParseState _state         = WAIT_AA;
    uint8_t    _expectedCount = 0;
    uint8_t    _boxesRecvd    = 0;
    uint8_t    _byteIdx       = 0;
    uint8_t    _checksum      = 0;
    K230DBox   _parsing       = {};

    K230DBox   _boxes[K230D_MAX_BOXES_RX] = {};
    uint8_t    _boxCount      = 0;
    uint32_t   _lastPacketMs  = 0;
};
