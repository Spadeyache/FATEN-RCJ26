#pragma once

// =============================================================================
//  Actions::Turn — universal speed-scaled in-place spin.
//
//  turn(angle_deg, speed = MAX_MOTOR_SPEED):
//    angle_deg > 0 → right   |   angle_deg < 0 → left
//    speed         → motor power (1..MAX_MOTOR_SPEED)
//    duration = |angle| × TURN_SPIN_MS_PER_DEG × MAX_MOTOR_SPEED / speed
//    Same scaling rule as Forward::forward() — calibrate TURN_SPIN_MS_PER_DEG
//    at MAX_MOTOR_SPEED once and all other speeds derive automatically.
//    Blocking — motors stopped on exit.
// =============================================================================

namespace Actions {
namespace Turn {

void turn(float angle_deg, float speed = 65.0f);
void turnRaw(float angle_deg, float speed = 65.0f);
bool turnUntilCenterPoint(float angleSign,
                          float speed,
                          unsigned long timeoutMs);
bool turnUntilCenterPoint(float angleSign,
                          float speed);
// Spin until the center point is seen, giving up after the time an in-place
// turn of maxAngleDeg would take (same speed scaling as turn()).
bool turnUntilCenterPointMaxDeg(float angleSign,
                                float speed,
                                float maxAngleDeg);

// Same spin, but only the front ARC (top) ends it - the bottom row is
// ignored (XIAO_FLAG_TOP_LINE instead of FEAT_CENTER_POINT_BLACK).
bool turnUntilCenterPointArc(float angleSign,
                             float speed,
                             unsigned long timeoutMs);

}  // namespace Turn
}  // namespace Actions
