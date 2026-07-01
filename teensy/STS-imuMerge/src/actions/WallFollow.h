#pragma once

// =============================================================================
//  Actions::WallFollow
//
//  Single ToF-based right-wall follower used by EVAC_Exit and EVAC_Entry.
//
//  tick(targetMm, baseSpeed, farMm, detectSudden):
//    - Reads the shared tofFL[8][8] grid populated by Sensors::ToF::tick().
//    - Runs a PID on the selected 2x2 ToF block when a wall is visible.
//    - Drives straight (blind search) while the wall is temporarily absent
//      (invalid reading, or distance >= farMm).
//    - Handles the front touch sensor internally: backs up and turns, then
//      returns TOUCH. The turn angle depends on the last returned status —
//      wide (established wall/pause) vs narrow (still blind-searching).
//    - detectSudden = true (EVAC_Exit): once a wall has been acquired and
//      then goes far/invalid, classifies an abrupt jump as
//      EXIT_CANDIDATE_SUDDEN (likely a real opening) instead of just NO_WALL.
//    - detectSudden = false (EVAC_Entry): that classification is skipped
//      entirely — any far/invalid reading is always just NO_WALL (drive
//      straight), no matter how it got there.
//
//  recover(backupMm):
//    Stop, back up backupMm, and turn away from the wall (same context-aware
//    angle touch uses), then reset(). For callers that want the same
//    "back off and turn" recovery touch gets, but for a different trigger
//    (e.g. a tape-color marker) and/or a different backup distance.
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
void recover(float backupMm);
Status tick(float targetMm = 100.0f, float baseSpeed = 40.0f,
            float farMm = 230.0f, bool detectSudden = true);

}  // namespace WallFollow
}  // namespace Actions
