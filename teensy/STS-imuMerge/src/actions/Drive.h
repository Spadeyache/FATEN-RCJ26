#pragma once

// =============================================================================
//  Actions::Drive — 4-wheel motor control + line-follow PID.
//
//  Owns:
//    - the yacheSTS instance (STS_SERIAL @ 1 Mbps)
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

enum LineFollowState : uint8_t {
    LINE_FOLLOW_FLAT = 0,
    LINE_FOLLOW_NOSE_UP,
    LINE_FOLLOW_NOSE_DOWN,
    LINE_FOLLOW_LEFT_DOWN,
    LINE_FOLLOW_RIGHT_DOWN,
};

void init();

// Set both sides; replicated L→FL/BL, R→FR/BR and clamped to ±MAX_MOTOR_SPEED.
void motor(float32_t left, float32_t right) FASTRUN;
void stop() FASTRUN;

// Slope-aware raw wheel output for behaviours that already picked left/right
// powers. turnNorm is [-1,+1]: + = right turn, - = left turn, magnitude = turn sharpness.
void motorSlopeProfiled(float32_t left,
                        float32_t right,
                        float32_t turnNorm,
                        bool applyTightSlowReverse = false);

// Turn-only IMU gravity compensation. This does not apply rot-axis front/rear
// scaling; it strengthens the motor sign that counters gravity for the current
// pitch/roll and turn direction.
void motorTurnGravityProfiled(float32_t left,
                              float32_t right,
                              float32_t turnNorm);

// Blocking spin-in-place that decays from |power| down to 35 over durationMs.
// left = +power, right = -power (so positive power spins one way). Stops at end.
void spinDecay(float32_t power, uint32_t durationMs);

// Debug/test only — drives the four wheels independently, no IMU comp,
// no L/R replication. Used for bench-testing individual motor wiring.
void motorRaw(float32_t fl, float32_t fr, float32_t bl, float32_t br) FASTRUN;

void runLinePID();
uint32_t scaledLinePidMs(uint32_t flatMs, uint32_t minMs, uint32_t maxMs);
LineFollowState lineFollowState();
const char* lineFollowStateName(LineFollowState state);

// Per-wheel gain accessors (read by Processing::Mapping for unicycle model).
float32_t frontLeftGain();
float32_t frontRightGain();
float32_t backLeftGain();
float32_t backRightGain();

}  // namespace Drive
}  // namespace Actions
