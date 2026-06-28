#pragma once

// =============================================================================
//  VictimManager — tracks balls held in the two arms, decides which arm to use,
//  runs the grab + a self-confirmation step, and records the grab sequence.
//
//  Arms (capacity): LEFT holds 2 (LIFO), RIGHT holds 1.
//  Natural arm:     alive/silver -> LEFT, dead/black -> RIGHT.
//                   Overflow to the other arm when the natural one is full,
//                   so any combination (3 silver, 3 black, mixed) fits in 3.
//  Confirmation:    after the grab, if a same-type ball is still seen tall
//                   (height >= EVAC_GRAB_STOP_HEIGHT_PX * 0.6) the grab failed
//                   and nothing is counted.
// =============================================================================

#include <Arduino.h>

namespace VictimManager {

void    reset();              // empty everything (start of an evac run)
uint8_t count();             // total balls held (0..3)
bool    full();              // count >= EVAC_MAX_BALLS

// Pick an arm for `type`, run the grab + self-confirmation. Records the ball and
// returns true only if confirmed captured; otherwise counts nothing and returns
// false (the caller just keeps searching).
bool    tryGrab(uint8_t type);

void    clearAll();          // empty everything (after a deploy)

}  // namespace VictimManager
