#ifndef yacheVL53L7CX_h
#define yacheVL53L7CX_h

// =============================================================================
//  yacheVL53L7CX — thin wrapper around STM32duino_VL53L7CX
//
//  Phase 1: single front sensor on Wire1 at default I2C address (0x52),
//  no LPn juggling. The class is array-ready: pass an `lpn_pin` to support
//  multiple sensors on the same bus in phase 2 (each gets its own address).
//
//  Coordinate convention:
//    - sensor frame: +x forward (away from sensor face), +y to sensor's left
//    - mount transform places the sensor in the robot frame:
//        x_robot = mountDx + r * cos(theta_zone + mountYaw)
//        y_robot = mountDy + r * sin(theta_zone + mountYaw)
//
//  Zone bearing convention (horizontal, in sensor frame):
//    - column index 0 = leftmost zone (positive bearing, +y side)
//    - column index res-1 = rightmost zone (negative bearing, -y side)
//    - bearing = +(FOV/2) - (col + 0.5) * (FOV/res)
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <vl53l7cx_class.h>
#include <arm_math.h>
#include "config.h"

class yacheVL53L7CX {
public:
    yacheVL53L7CX(TwoWire& bus, int lpn_pin = -1, int i2c_rst_pin = -1);

    // Returns true on success. Loads firmware, sets resolution + frequency, starts ranging.
    bool begin(uint8_t res_per_side = TOF_RES,
               uint8_t freq_hz      = TOF_FREQ_HZ,
               uint8_t i2c_addr     = 0x52);

    // True if a new frame is ready. Non-blocking.
    bool dataReady();

    // Pulls latest frame. mm_out / status_out are zone_count() entries each.
    // Returns true on success.
    bool getRanges(int16_t* mm_out, uint8_t* status_out);

    // Mount-transform setters (robot-frame placement of sensor origin).
    void setMountTransform(float dx_mm, float dy_mm, float yaw_rad);
    float mountDx()  const { return _dx; }
    float mountDy()  const { return _dy; }
    float mountYaw() const { return _yaw; }

    // Geometry helpers.
    uint8_t resolution() const { return _res; }                 // zones per side (4 or 8)
    uint16_t zoneCount() const { return (uint16_t)_res * _res; }

    // Horizontal bearing of zone `idx` in the sensor frame (radians, +left).
    float zoneBearing(uint16_t idx) const;

    // Convenience: a measurement is "valid" if status is 5 or 9 and mm is in range.
    static inline bool isValid(uint8_t status, int16_t mm_value) {
        return (status == 5 || status == 9)
            && mm_value >= TOF_MIN_MM
            && mm_value <= TOF_MAX_MM;
    }

private:
    VL53L7CX  _drv;
    uint8_t   _res;     // zones per side (4 or 8)
    float     _dx, _dy, _yaw;
    bool      _ready;
};

#endif
