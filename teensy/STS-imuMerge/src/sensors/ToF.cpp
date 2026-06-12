#include "ToF.h"
#include <Wire.h>

// =============================================================================
//  Sensors::ToF — 4x VL53L7CX on Wire1.
//
//  All VL53L7CX power up at the same default I2C address (0x52). To run several
//  on one bus we hold every sensor in reset via its XSHUT pin, then bring them
//  up ONE AT A TIME and assign each a unique address before enabling the next.
//  (Same idea as the classic VL53L0X multi-sensor XSHUT sequence.)
//
//  Sensor 0 has NO XSHUT wired (XSHUT pin = -1): it stays powered on. It is
//  therefore configured first and moved off the default 0x52, so that when the
//  XSHUT sensors are later woken (and boot at 0x52) there is no collision.
// =============================================================================

namespace Sensors {
namespace ToF {

namespace {
    // lpn=-1: we drive XSHUT ourselves, so the driver must not toggle it.
    yacheVL53L7CX _tof[TOF_COUNT] = {
        yacheVL53L7CX(Wire1, -1, -1),
        yacheVL53L7CX(Wire1, -1, -1),
        yacheVL53L7CX(Wire1, -1, -1),
        yacheVL53L7CX(Wire1, -1, -1),
    };

    // -1 = no XSHUT pin (sensor is always powered on).
    const int8_t _xshut[TOF_COUNT] = {
        TOF0_XSHUT_PIN, TOF1_XSHUT_PIN, TOF2_XSHUT_PIN, TOF3_XSHUT_PIN
    };
    const uint8_t _addr[TOF_COUNT] = {
        TOF0_ADDR, TOF1_ADDR, TOF2_ADDR, TOF3_ADDR
    };
    const float _dx[TOF_COUNT] = {
        TOF0_DX_MM, TOF1_DX_MM, TOF2_DX_MM, TOF3_DX_MM
    };
    const float _dy[TOF_COUNT] = {
        TOF0_DY_MM, TOF1_DY_MM, TOF2_DY_MM, TOF3_DY_MM
    };
    const float _yaw_deg[TOF_COUNT] = {
        TOF0_YAW_DEG, TOF1_YAW_DEG, TOF2_YAW_DEG, TOF3_YAW_DEG
    };

    bool _initialised = false;

    inline float deg2rad(float d) { return d * (float)PI / 180.0f; }
}

void init() {
    if (_initialised) return;
    Wire1.begin();   // idempotent — Sensors::IMU also calls this

    // 1. Hold every XSHUT sensor in reset (low = off). Sensors with no XSHUT
    //    (pin == -1) stay powered on at the default 0x52.
    for (uint8_t i = 0; i < TOF_COUNT; ++i) {
        if (_xshut[i] < 0) continue;
        pinMode(_xshut[i], OUTPUT);
        digitalWrite(_xshut[i], LOW);
    }
    delay(10);

    // 2. Configure one at a time. Index 0 (no XSHUT) is done first and moved off
    //    0x52; each later XSHUT sensor is then woken alone at 0x52 and reassigned.
    for (uint8_t i = 0; i < TOF_COUNT; ++i) {
        if (_xshut[i] >= 0) {
            digitalWrite(_xshut[i], HIGH);
            delay(10);                   // let it boot before talking to it
        }

        _tof[i].setMountTransform(_dx[i], _dy[i], deg2rad(_yaw_deg[i]));
        if (!_tof[i].begin(TOF_RES, TOF_FREQ_HZ, _addr[i])) {
            Serial.printf("[ToF] sensor %u init FAILED\n", i);
        } else {
            Serial.printf("[ToF] sensor %u ready @0x%02X\n", i, _addr[i]);
        }
    }

    _initialised = true;
}

yacheVL53L7CX& sensor(uint8_t i) { return _tof[i]; }
uint8_t        count()           { return TOF_COUNT; }

}  // namespace ToF
}  // namespace Sensors
