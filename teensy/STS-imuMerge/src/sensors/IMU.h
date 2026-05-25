#pragma once

// =============================================================================
//  Sensors::IMU — MPU-6050 + Madgwick wrapper.
//  Owns one yacheMPU6050 on Wire1. tick() is internally throttled to
//  IMU_SAMPLE_HZ, so safe to call every loop iteration.
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
