#include "ToF.h"
#include "../../pins_teensy.h"
#include <Wire.h>

int16_t tofFL[8][8];

// =============================================================================
//  Sensors::ToF - VL53L7CX front-left sensor on TOF_WIRE.
//
//  Current bring-up wiring: one VL53L7CX connected directly on Wire1 at the
//  default ST 8-bit address 0x52 (scanner shows 7-bit 0x29), no XSHUT/LPn pin.
// =============================================================================

namespace Sensors {
namespace ToF {

namespace {
    constexpr uint32_t TOF_INIT_I2C_HZ = 400000;  // Firmware upload speed.
    constexpr uint32_t TOF_RUN_I2C_HZ  = 400000;  // Runtime ranging/read speed.

    yacheVL53L7CX _tof[TOF_COUNT] = {
        yacheVL53L7CX(TOF_WIRE, -1, -1),
        yacheVL53L7CX(TOF_WIRE, -1, -1),
        yacheVL53L7CX(TOF_WIRE, -1, -1),
        yacheVL53L7CX(TOF_WIRE, -1, -1),
    };

    bool _initialised = false;

    inline float deg2rad(float d) { return d * (float)PI / 180.0f; }

    void clearFL() {
        for (uint8_t row = 0; row < 8; ++row) {
            for (uint8_t col = 0; col < 8; ++col) {
                tofFL[row][col] = -1;
            }
        }
    }
}

void init() {
    if (_initialised) return;
    TOF_WIRE.begin();   // ToF has its own bus; see TOF_WIRE in pins_teensy.h.
    TOF_WIRE.setClock(TOF_INIT_I2C_HZ);
    delay(20);
    clearFL();

    _tof[0].setMountTransform(TOF0_DX_MM, TOF0_DY_MM, deg2rad(TOF0_YAW_DEG));
    const uint32_t t0 = millis();
    if (!_tof[0].begin(8, TOF_FREQ_HZ, 0x52)) {
        Serial.println("[ToF] FL init FAILED @0x52");
    } else {
        TOF_WIRE.setClock(TOF_RUN_I2C_HZ);
        Serial.printf("[ToF] FL ready @0x52 in %lu ms\n", (unsigned long)(millis() - t0));
    }

    _initialised = true;
}

bool tick() {
    if (!_initialised) return false;
    if (!_tof[0].dataReady()) return false;

    int16_t mm[64];
    uint8_t status[64];
    if (!_tof[0].getRanges(mm, status)) return false;

    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t col = 0; col < 8; ++col) {
            const uint8_t zone = row * 8 + col;
            tofFL[row][col] = yacheVL53L7CX::isValid(status[zone], mm[zone])
                ? mm[zone]
                : -1;
        }
    }
    return true;
}

void printFL() {
    Serial.println("tofFL mm (-1 = invalid):");
    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t col = 0; col < 8; ++col) {
            Serial.print(tofFL[row][col]);
            Serial.print(col < 7 ? "\t" : "\n");
        }
    }
    Serial.println("---");
}

yacheVL53L7CX& sensor(uint8_t i) { return _tof[i]; }
uint8_t        count()           { return TOF_COUNT; }

}  // namespace ToF
}  // namespace Sensors
