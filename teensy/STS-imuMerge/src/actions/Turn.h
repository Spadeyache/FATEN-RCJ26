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

}  // namespace Turn
}  // namespace Actions
