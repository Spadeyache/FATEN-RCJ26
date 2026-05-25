#pragma once

// =============================================================================
//  Actions::Drive — 4-wheel motor control + line-follow PID.
//
//  Owns:
//    - the yacheSTS instance (Serial2 @ 1 Mbps)
//    - the four per-wheel gain globals (volatile, written by motor())
//    - the 9 ms IntervalTimer ISR that pushes gains to the servos
//
//  Public API:
//    motor(L, R)   — set both sides; clamped to ±MAX_MOTOR_SPEED.
//    stop()        — convenience for motor(0, 0).
//    runLinePID()  — one tick of the line-follow PID using XiaoDecode::lineError().
// =============================================================================

#include <arm_math.h>

namespace Actions {
namespace Drive {

void init();

void motor(float32_t left, float32_t right) FASTRUN;
void stop() FASTRUN;

void runLinePID();

// Per-wheel gain accessors (read by Processing::Mapping for unicycle model).
float32_t frontLeftGain();
float32_t frontRightGain();
float32_t backLeftGain();
float32_t backRightGain();

}  // namespace Drive
}  // namespace Actions
