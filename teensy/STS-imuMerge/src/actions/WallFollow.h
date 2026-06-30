#pragma once

// =============================================================================
//  Actions::WallFollow
//
//  Single ToF-based right-wall follower used by EVAC_Exit.
//
//  tick(targetMm, baseSpeed):
//    - Reads the shared tofFL[8][8] grid populated by Sensors::ToF::tick().
//    - Runs a PID on the selected 2x2 ToF block when a wall is visible.
//    - Drives straight while the wall is temporarily absent.
//    - Returns EXIT_CANDIDATE once the wall has been acquired and then becomes
//      far/invalid for enough frames.
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
    EXIT_CANDIDATE,
};

void reset();
Status tick(float targetMm = 100.0f, float baseSpeed = 40.0f);

}  // namespace WallFollow
}  // namespace Actions
