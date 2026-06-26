#pragma once

// =============================================================================
//  Actions::Arm — grip (hobby servos) + lift (KRS smart servo) control.
//
//  init():
//    Attaches both HS-45HB hobby servos, opens the gripper, attaches the KRS
//    lift (PWM mode) and parks it, then DETACHES the hobby servos to silence
//    them. Re-attach happens automatically on the first grab() call via
//    attach() inside those functions if needed (see Arm.cpp). The KRS stays
//    attached so it holds its parked pose.
//
//  grab(closed):  closed=true → grip closed (1700/1300 µs)
//                 closed=false → grip open  (1000/2000 µs)
//  lift(us):      KRS PWM pulse width in microseconds (clamped to
//                 KRS_PWM_MIN_US..KRS_PWM_MAX_US). Blocking ~800 ms, then holds.
// =============================================================================

namespace Actions {
namespace Arm {

void init();

void grab(bool closed);
void grabLeft(bool closed, bool blocking = true);    // black/dead arm
void grabRight(bool closed, bool blocking = true);   // silver/alive arm
void lift(int us);   // KRS PWM pulse width (microseconds)

// Evacuation-zone victim handling.
//   captureDead()/captureAlive(): take one ball into storage via the matching
//     arm (black=dead, silver=alive). Blocking, open-loop.
//   releaseAll(): open everything to drop all held balls at the corner.
// NOTE: Phase 1 placeholders — both capture calls currently drive the single
// existing gripper. TODO: wire the dedicated black/silver arms in Arm.cpp.
void captureDead();
void captureAlive();
void releaseAll();

// Manual servo attach/detach — used when re-engaging the gripper inside
// long sequences after init() detached it.
void attachServos();
void detachServos();

}  // namespace Arm
}  // namespace Actions
