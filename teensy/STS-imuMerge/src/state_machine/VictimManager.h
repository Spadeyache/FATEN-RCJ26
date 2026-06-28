#pragma once

// =============================================================================
//  VictimManager — tracks balls held in the two arms, decides which arm to use,
//  runs the grab + a self-confirmation step, and records the grab sequence.
//
//  Arms (capacity): LEFT holds 2 (LIFO via bucket), RIGHT holds 1.
//  Placement:       ALIVE -> LEFT first, then RIGHT.  DEAD -> RIGHT only, and
//                   at most one dead total (search stops chasing dead after one).
//  Confirmation:    after the grab, if a same-type ball is still seen tall
//                   (height >= EVAC_GRAB_STOP_HEIGHT_PX * 0.6) the grab failed
//                   and nothing is counted.
// =============================================================================

#include <Arduino.h>

namespace VictimManager {

void    reset();              // empty everything (start of an evac run)
void    clearAll();          // empty everything (after a deploy)

uint8_t count();             // total balls held
uint8_t liveHeld();          // silver/alive balls held
uint8_t deadHeld();          // dead/black balls held (0 or 1)
bool    full();              // both arms full

// Should the search loop chase a victim of this type right now?
//   DEAD  : only if we hold no dead yet AND the right arm is free.
//   ALIVE : whenever any arm has space.
bool    acceptsType(uint8_t type);

// Stop collecting and deploy once we hold this many live balls.
bool    readyToDeploy();

// Pick an arm for `type`, run the grab + self-confirmation. Records the ball and
// returns true only if confirmed captured; otherwise counts nothing and returns
// false (the caller just keeps searching).
bool    tryGrab(uint8_t type);

// Deploy releases (the manager knows which arm holds what).
void    releaseLive();       // drop all live balls (green corner), clear them
void    releaseDead();       // drop the dead ball (red corner), clear it

}  // namespace VictimManager
