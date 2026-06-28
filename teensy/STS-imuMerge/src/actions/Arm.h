#pragma once

#include <stdint.h>

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

// =============================================================================
//  Pure hardware primitives (blocking servo moves). NO vision, NO grab logic —
//  the capture choreography lives in VictimManager, which calls these.
// =============================================================================
namespace Actions {
namespace Arm {

void init();

// Grippers (silver/alive = LEFT, dead/black = RIGHT).
void grab(bool closed);                              // both grippers
void grabLeft(bool closed, bool blocking = true);
void grabRight(bool closed, bool blocking = true);
void releaseAll();                                   // open both (drop held balls)

// Lift (KRS, PWM). lift() sets a raw pulse; the named helpers are the poses.
void lift(int us);
void liftDown();      // lower to grab pose
void liftCarry();     // raise to carry pose
void liftPark();      // parked pose

void attachServos();
void detachServos();

}  // namespace Arm
}  // namespace Actions
