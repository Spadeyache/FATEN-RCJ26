#pragma once

// =============================================================================
//  Sensors::IMU — MPU-6050 + Madgwick wrapper.
//  Owns one yacheMPU6050 on Wire (I2C1). Sampling runs off a hardware
//  IntervalTimer at IMU_SAMPLE_HZ, independent of the main loop — delay()s
//  elsewhere no longer starve the filter. tick() only drives the optional
//  PRINT_IMU debug print now; calling it (or not) doesn't affect freshness
//  of getPitch/getRoll/getYaw, so all existing call sites stay harmless.
// =============================================================================

#include <arm_math.h>

namespace Sensors {
namespace IMU {

void init();
void tick();

float32_t getPitch();   // degrees
float32_t getRoll();    // degrees
float32_t getYaw();     // degrees, wrapped to ±180°

}  // namespace IMU
}  // namespace Sensors
