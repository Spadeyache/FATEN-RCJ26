#pragma once

// =============================================================================
//  yacheVL53L7CX â€” thin wrapper around STM32duino_VL53L7CX.
//
//  Phase 1: single front sensor on Wire1 at default IÂ²C address (0x52),
//  no LPn juggling. Array-ready for phase 2 (multi-sensor on same bus).
//
//  Sensor frame:  +x forward,  +y to sensor's left.
//  Mount transform places sensor origin in the robot frame:
//      x_robot = mountDx + r * cos(theta_zone + mountYaw)
//      y_robot = mountDy + r * sin(theta_zone + mountYaw)
//
//  Zone bearing (horizontal, sensor-frame):
//      bearing = +(FOV/2) - (col + 0.5) * (FOV/res)
//    col 0  = leftmost  (+bearing)
//    col R-1= rightmost (-bearing)
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <vl53l7cx_class.h>
#include <arm_math.h>
#include "../../pins_teensy.h"

class yacheVL53L7CX {
public:
    yacheVL53L7CX(TwoWire& bus, int lpn_pin = -1, int i2c_rst_pin = -1);

    bool begin(uint8_t res_per_side = TOF_RES,
               uint8_t freq_hz      = TOF_FREQ_HZ,
               uint8_t i2c_addr     = 0x52);

    bool dataReady();
    bool getRanges(int16_t* mm_out, uint8_t* status_out);

    void  setMountTransform(float dx_mm, float dy_mm, float yaw_rad);
    float mountDx()  const { return _dx; }
    float mountDy()  const { return _dy; }
    float mountYaw() const { return _yaw; }

    uint8_t  resolution() const { return _res; }
    uint16_t zoneCount()  const { return (uint16_t)_res * _res; }
    float    zoneBearing(uint16_t idx) const;

    // status 5 or 9 = good reading, mm within [TOF_MIN_MM, TOF_MAX_MM].
    static inline bool isValid(uint8_t status, int16_t mm_value) {
        return (status == 5 || status == 9)
            && mm_value >= TOF_MIN_MM
            && mm_value <= TOF_MAX_MM;
    }

private:
    VL53L7CX  _drv;
    uint8_t   _res;
    float     _dx, _dy, _yaw;
    bool      _ready;
};
