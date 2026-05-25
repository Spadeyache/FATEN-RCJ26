#pragma once

// =============================================================================
//  Actions::Forward — drive straight a calibrated distance.
//
//  forward(speed, distance_mm, useIMU = false, pumpComms = false):
//    speed       : motor power 0..MAX_MOTOR_SPEED (signed — negative reverses)
//    distance_mm : converted to time via FORWARD_MS_PER_MM, scaled by speed
//    useIMU      : true → yaw-hold P-controller corrects drift each tick
//    pumpComms   : true → service XIAO_link + XiaoDecode during the move
//                  (used inside touch/conduct sub-sequences that need fresh
//                   xiaoCommand to decide when to abort)
//
//  All variants are blocking; motor is stopped on exit.
// =============================================================================

namespace Actions {
namespace Forward {

void forward(float speed,
             float distance_mm,
             bool  useIMU    = false,
             bool  pumpComms = false);

}  // namespace Forward
}  // namespace Actions
