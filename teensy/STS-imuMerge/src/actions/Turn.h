#pragma once

// =============================================================================
//  Actions::Turn — time-based blocking turns.
//
//  turn(angle):
//    angle > 0 → right turn   |   angle < 0 → left turn
//    Duration = |angle| × TURN_MS_PER_DEG  (calibrated in config.h)
//    Motor speeds = TURN_LEFT_* / TURN_RIGHT_* from config.h
//    Stops motors and returns when duration elapses.
//
//  uTurn():
//    Fixed-duration 180° spin (TURN_UTURN_MS). Blocking.
// =============================================================================

namespace Actions {
namespace Turn {

void turn(float angle_deg);
// void uTurn();

}  // namespace Turn
}  // namespace Actions
