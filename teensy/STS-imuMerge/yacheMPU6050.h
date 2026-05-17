#ifndef yacheMPU6050_h
#define yacheMPU6050_h

#include "Arduino.h"
#include <Wire.h>
#include "I2Cdev.h"
#include "MPU6050.h"
#include <MadgwickAHRS.h>
#include <arm_math.h>
#include "config.h"

// =============================================================================
//  yacheMPU6050 — MPU-6050 + Madgwick filter (no DMP)
//  Mirrors the proven MPU6050-ALL03-yzplane reference sketch:
//    - raw getMotion6() reads at IMU_SAMPLE_HZ
//    - Madgwick.updateIMU() (gyro+accel only, no magnetometer)
//    - getPitch/getRoll/getYaw return degrees, zero-referenced to begin()
// =============================================================================

#ifndef IMU_SAMPLE_HZ
#define IMU_SAMPLE_HZ 25.0f   // Reference sketch ran at 25 Hz; Madgwick beta tuned for this
#endif

class yacheMPU6050 {
public:
    explicit yacheMPU6050(TwoWire &w = Wire);

    void begin();
    void update() FASTRUN;     // 25 Hz internal throttle; safe to call every loop
    void calibrate();          // Auto-calibrate offsets; prints results to Serial
    void zeroAttitude();       // Snap current yaw/pitch/roll as zero reference
    void printQuat() FLASHMEM;

    // EEPROM persistence — direct port of save()/olddata() from
    // MPU6050-ALL03-yzplane. Layout: 6x int @ addresses 0,4,8,12,16,20.
    // Sits inside the calib block reserved at 0x0000-0x001B (config.h).
    bool loadOffsetsFromEEPROM();   // matches olddata(); always returns true (no magic check)
    void saveOffsetsToEEPROM();     // matches save()

    float32_t getPitch() { return _pitch - _pitchZero; }
    float32_t getRoll()  { return _roll  - _rollZero;  }
    float32_t getYaw();        // wraps to ±180° after zero subtract

private:
    TwoWire   *_wire;
    MPU6050    _mpu;
    Madgwick   _filter;

    uint32_t   _microsPerReading = 0;
    uint32_t   _microsPrevious   = 0;

    float32_t _pitch = 0.0f, _roll = 0.0f, _yaw = 0.0f;
    float32_t _yawZero = 0.0f, _pitchZero = 0.0f, _rollZero = 0.0f;

    // Hardcoded offsets — used when CALIBRATE_IMU == 0.
    // From the working MPU6050-ALL03-yzplane reference; re-run calibrate() for a
    // different physical sensor and paste the printed values here.
    int16_t ax_offset = -4737, ay_offset = -374, az_offset = 631;
    int16_t gx_offset =    19, gy_offset =   54, gz_offset =   2;

    int32_t buffersize    = 1000;
    int16_t acel_deadzone = 8;
    int16_t giro_deadzone = 1;
    int32_t mean_ax, mean_ay, mean_az, mean_gx, mean_gy, mean_gz;
    int16_t axRaw, ayRaw, azRaw, gxRaw, gyRaw, gzRaw;

    static float convertRawAccel(int16_t a) { return (float)a / 16384.0f; }  // ±2g
    static float convertRawGyro (int16_t g) { return (float)g / 131.0f;   }  // ±250°/s

    void applyOffsets();
    void meansensors();
    void runAutoCalibration();
    void sampleAndFilterOnce();   // Forces one filter step at the current sample window
};

#endif
