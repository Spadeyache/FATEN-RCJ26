#ifndef yacheMPU6050_h
#define yacheMPU6050_h

#include "Arduino.h"
#include <Wire.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include <arm_math.h>
#include "config.h"

class yacheMPU6050 {
public:
    // Pass the Wire bus at construction — DMP needs it at object creation time.
    explicit yacheMPU6050(TwoWire &w = Wire);

    void begin();              // DMP init + offset apply + auto-zero
    void update() FASTRUN;     // Poll DMP FIFO; updates pitch/roll/yaw if new packet ready
    void calibrate();
    void zeroAttitude();       // Capture current orientation as zero reference (called by begin())
    void printQuat() FLASHMEM;

    float32_t getPitch() { return _pitch; }
    float32_t getRoll()  { return _roll; }
    float32_t getYaw()   { return _yaw; }

private:
    TwoWire                    *_wire;
    MPU6050_6Axis_MotionApps20  _mpu;
    bool                        _dmpReady = false;
    uint8_t                     _fifo[64];

    float32_t _pitch = 0.0f, _roll = 0.0f, _yaw = 0.0f;
    float32_t _yawZero = 0.0f, _pitchZero = 0.0f, _rollZero = 0.0f;

    // Hardcoded calibration offsets.
    // Run with CALIBRATE_IMU=1 to measure, then paste the printed values here.
    int16_t ax_offset = 1775, ay_offset = 455,  az_offset = 1598;
    int16_t gx_offset = 79,   gy_offset = 13,   gz_offset = -46;

    int32_t buffersize    = 1000;
    int16_t acel_deadzone = 8;
    int16_t giro_deadzone = 1;
    int32_t mean_ax, mean_ay, mean_az, mean_gx, mean_gy, mean_gz;
    int16_t axRaw, ayRaw, azRaw, gxRaw, gyRaw, gzRaw;

    void applyOffsets();
    void meansensors();
    void runAutoCalibration();
    void _computeYPR(float &yaw, float &pitch, float &roll);  // raw DMP→degrees, no filter/zero
};

#endif
