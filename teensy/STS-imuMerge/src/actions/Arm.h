#pragma once

// =============================================================================
//  Actions::Arm — grip (hobby servos) + lift (KRS smart servo) control.
//
//  init():
//    Attaches both HS-45HB hobby servos, opens the gripper, initialises the
//    KRS bus, parks the lift, then DETACHES the hobby servos to silence
//    them. Re-attach happens automatically on the first grab()/lift() call
//    via attach() inside those functions if needed (see Arm.cpp).
//
//  grab(closed):  closed=true → grip closed (1700/1300 µs)
//                 closed=false → grip open  (1000/2000 µs)
//  lift(pos):     KRS position 3500..11500. Blocking 800 ms then setFree().
// =============================================================================

namespace Actions {
namespace Arm {

void init();

void grab(bool closed);
void lift(int pos);

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
void attachGrabServos();
void detachGrabServos();

}  // namespace Arm
}  // namespace Actions
