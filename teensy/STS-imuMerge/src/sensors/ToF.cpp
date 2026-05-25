#include "ToF.h"
#include <Wire.h>

namespace Sensors {
namespace ToF {

namespace {
    yacheVL53L7CX _tof[TOF_COUNT] = {
        yacheVL53L7CX(Wire1, TOF_LPN_PIN_FRONT, TOF_I2C_RST_PIN),
    };
    bool _initialised = false;
}

void init() {
    if (_initialised) return;
    Wire1.begin();   // idempotent — Sensors::IMU also calls this
    _tof[0].setMountTransform(TOF_FRONT_DX_MM, TOF_FRONT_DY_MM, TOF_FRONT_YAW_RAD);
    if (!_tof[0].begin()) {
        Serial.println(F("[ToF] front init FAILED"));
    } else {
        Serial.println(F("[ToF] front ready"));
    }
    _initialised = true;
}

yacheVL53L7CX& sensor(uint8_t i) { return _tof[i]; }
uint8_t        count()           { return TOF_COUNT; }

}  // namespace ToF
}  // namespace Sensors
