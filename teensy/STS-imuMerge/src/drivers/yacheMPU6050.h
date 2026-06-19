#pragma once

// =============================================================================
//  yacheMPU6050 â€” MPU-6050 + Madgwick filter (no DMP).
//
//  - Raw getMotion6() reads at IMU_SAMPLE_HZ.
//  - Madgwick.updateIMU() (gyro + accel only, no magnetometer).
//  - getPitch/getRoll/getYaw return degrees, zero-referenced to begin().
//  - Calibration offsets persist to EEPROM at addresses 0..23.
// =============================================================================

#include "Arduino.h"
#include <Wire.h>
#include "I2Cdev.h"
#include "MPU6050.h"
#include <MadgwickAHRS.h>
#include <arm_math.h>
#include "../../config.h"

#ifndef IMU_SAMPLE_HZ
#define IMU_SAMPLE_HZ 25.0f
#endif

class yacheMPU6050 {
public:
    explicit yacheMPU6050(TwoWire &w = Wire);

    void begin();
    void update() FASTRUN;
    void calibrate();
    void zeroAttitude();
    void printQuat() FLASHMEM;

    bool loadOffsetsFromEEPROM();
    void saveOffsetsToEEPROM();

    float32_t getPitch() { return _pitch - _pitchZero; }
    float32_t getRoll()  { return _roll  - _rollZero;  }
    float32_t getYaw();

private:
    TwoWire   *_wire;
    MPU6050    _mpu;
    Madgwick   _filter;

    uint32_t   _microsPerReading = 0;
    uint32_t   _microsPrevious   = 0;

    float32_t _pitch = 0.0f, _roll = 0.0f, _yaw = 0.0f;
    float32_t _yawZero = 0.0f, _pitchZero = 0.0f, _rollZero = 0.0f;

    int16_t ax_offset = IMU_AX_OFFSET, ay_offset = IMU_AY_OFFSET, az_offset = IMU_AZ_OFFSET;
    int16_t gx_offset = IMU_GX_OFFSET, gy_offset = IMU_GY_OFFSET, gz_offset = IMU_GZ_OFFSET;

    int32_t buffersize    = 1000;
    int16_t acel_deadzone = 8;
    int16_t giro_deadzone = 1;
    int32_t mean_ax, mean_ay, mean_az, mean_gx, mean_gy, mean_gz;
    int16_t axRaw, ayRaw, azRaw, gxRaw, gyRaw, gzRaw;

    static float convertRawAccel(int16_t a) { return (float)a / 16384.0f; }
    static float convertRawGyro (int16_t g) { return (float)g / 131.0f;   }

    void applyOffsets();
    void meansensors();
    void runAutoCalibration();
    void sampleAndFilterOnce();
};
