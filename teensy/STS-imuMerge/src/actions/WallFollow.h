#pragma once

// =============================================================================
//  Actions::WallFollow — single-sensor ToF wall-follow (right-facing VL53L7CX).
//
//  Physical setup: a ToF sensor mounted facing 90° to the robot's right. This
//  module holds the robot at a fixed standoff distance from a wall on its
//  right by running a PID on the sensor's center-zone distance and steering
//  via Actions::Drive::motor(left, right).
//
//  tick(targetMm, baseSpeed):
//    Call once per loop() — same non-blocking pattern as Drive::runLinePID().
//    Reads the global tofFL[8][8] grid (populated by Sensors::ToF::tick(),
//    which must run earlier in the same loop). On a valid reading, drives the
//    PID correction and returns true. On no valid reading (out of range / no
//    wall), stops the motors and returns false — caller decides what to do
//    next (re-acquire, hold last heading, etc.).
//
//    Front-bumper override: if Sensors::Touch::front() reads triggered (needs
//    Sensors::Touch::init()/tick() running — see STS-imuMerge.ino), tick()
//    blocks to back up, turn 90° left, then returns false. The PID resumes
//    normally on the next call.
//
//  To use later (none of this is wired into loop() yet):
//    1. #include "src/actions/WallFollow.h" in STS-imuMerge.ino
//    2. Ensure Sensors::ToF::init() + Sensors::Touch::init() run in setup(),
//       and Sensors::ToF::tick() + Sensors::Touch::tick() run in loop()
//       BEFORE WallFollow::tick().
//    3. Call Actions::WallFollow::tick() once per loop from the relevant state.
//    All PID/standoff tuning lives in WallFollow.cpp (anonymous namespace) —
//    no config.h changes required.
// =============================================================================

namespace Actions {
namespace WallFollow {

bool tick(float targetMm = 100.0f, float baseSpeed = 40.0f);

}  // namespace WallFollow
}  // namespace Actions
