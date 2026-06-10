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

#include <Arduino.h>
#include <arm_math.h>

namespace Actions {
namespace Drive {

void init();

// imuCompensation: when true, applies pitch/roll-based per-wheel gain from
// WeightDistribution.h. Defaults to false (current behaviour).
void motor(float32_t left, float32_t right, bool imuCompensation = false) FASTRUN;
void stop() FASTRUN;

// Debug/test only — drives the four wheels independently, no IMU comp,
// no L/R replication. Used for bench-testing individual motor wiring.
void motorRaw(float32_t fl, float32_t fr, float32_t bl, float32_t br) FASTRUN;

void runLinePID();

// Overcome stiction on one wheel by alternating its speed sign.
// motorIdx:     0=FL, 1=FR, 2=BL, 3=BR.
// amplitude:    ±speed (0..100) during the burst.
// cycles:       number of half-periods (total time = cycles × halfPeriodMs).
// halfPeriodMs: must be ≥ servo update period (~100 ms @ 10 Hz) so each
//               direction is actually executed before the flip.
void vibrateMotor(uint8_t motorIdx, float32_t amplitude = 30.0f, uint8_t cycles = 4, uint32_t halfPeriodMs = 100);

// Per-wheel gain accessors (read by Processing::Mapping for unicycle model).
float32_t frontLeftGain();
float32_t frontRightGain();
float32_t backLeftGain();
float32_t backRightGain();

}  // namespace Drive
}  // namespace Actions
