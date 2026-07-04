#pragma once

// =============================================================================
//  Actions::WallFollow
//
//  Single ToF-based right-wall follower.
//
//  tick(targetMm, baseSpeed, farMm, detectSudden):
//    - Reads the shared tofFL[8][8] grid populated by Sensors::ToF::tick().
//    - Runs a PID on the selected 2x2 ToF block when a wall is visible.
//    - Drives straight (blind search) while the wall is temporarily absent
//      (invalid reading, or distance >= farMm) — unless stopOnNoWall is set,
//      in which case it stops instead and just keeps reporting NO_WALL.
//    - Handles the front touch sensor internally: backs up and turns, then
//      returns TOUCH. The turn angle depends on the last returned status —
//      wide (established wall/pause) vs narrow (still blind-searching).
//    - detectSudden = true: once a wall has been acquired and
//      then goes far/invalid, classifies an abrupt jump as
//      EXIT_CANDIDATE_SUDDEN (likely a real opening) instead of just NO_WALL.
//    - detectSudden = false: that classification is skipped
//      entirely — any far/invalid reading is always just NO_WALL (drive
//      straight), no matter how it got there.
//
//  recover(backupMm):
//    Stop, back up backupMm, and turn away from the wall (same context-aware
//    angle touch uses - wide vs narrow, see tick() above), then reset(). For
//    callers that want the same "back off and turn" recovery touch gets, but
//    for a different trigger and/or a different backup distance.
//
//  recover(backupMm, turnDeg):
//    Same, but with an explicit turn angle instead of the context-aware one.
//    For triggers where the turn should always be the same regardless of
//    what WallFollow was doing when it fired (e.g. a tape-color marker,
//    which should always turn the same way whether it's seen while following
//    or while blind-searching).
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

// One-shot read of the wall ToF zone (same 2x2 block tick() uses).
// Returns false when every cell is invalid (-1); true + distance otherwise.
bool wallDistanceMm(float& outMm);

void reset();
void recover(float backupMm);
void recover(float backupMm, float turnDeg);
Status tick(float targetMm = 100.0f, float baseSpeed = 40.0f,
            float farMm = 230.0f, bool detectSudden = true,
            bool stopOnNoWall = false);

}  // namespace WallFollow
}  // namespace Actions
