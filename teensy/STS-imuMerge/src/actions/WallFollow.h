#pragma once

// =============================================================================
//  Actions::WallFollow
//
//  Single ToF-based right-wall follower used by EVAC_Exit.
//
//  tick(targetMm, baseSpeed):
//    - Reads the shared tofFL[8][8] grid populated by Sensors::ToF::tick().
//    - Runs a PID on the selected 2x2 ToF block when a wall is visible.
//    - Drives straight (blind search) while the wall is temporarily absent.
//    - Handles the front touch sensor internally: backs up and turns, then
//      returns TOUCH. The turn angle depends on the last returned status —
//      wide (established wall/pause) vs narrow (still blind-searching).
//    - Once a wall has been acquired and then goes far/invalid, classifies the
//      loss as EXIT_CANDIDATE_SUDDEN (an abrupt jump — likely a real opening)
//      or otherwise just keeps reporting NO_WALL (a gradual drift — the blind
//      search drive already handles that case).
//
//  reset():
//    Clears PID memory and exit-detection counters. Call before starting
//    wall-follow and after any blocking recovery maneuver.
// =============================================================================

namespace Actions {
namespace WallFollow {

enum class Status {
    FOLLOWING,
    NO_WALL,
    EXIT_CANDIDATE_SUDDEN,
    TOUCH,
};

void reset();
Status tick(float targetMm = 100.0f, float baseSpeed = 40.0f);

}  // namespace WallFollow
}  // namespace Actions
