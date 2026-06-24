#pragma once

// =============================================================================
//  EvacContext — evacuation-zone state shared across the EVAC_* states.
//
//  Tracks how many victims are currently held (split by type so the deposit
//  flow can route them once colour ID exists) and the 2-minute collection
//  timer that starts when EVAC_SEARCH begins.
// =============================================================================

#include <Arduino.h>

namespace EvacContext {

// Clear held counts and the timed-out flag. Call once on evac entry.
void reset();

// Start / query the collection window (EVAC_SEARCH_TIMEOUT_MS from this call).
void startSearchTimer();
bool searchTimedOut();

uint8_t heldTotal();
uint8_t heldDead();
uint8_t heldAlive();
bool    full();          // heldTotal >= EVAC_MAX_BALLS

void    addDead();
void    addAlive();
void    clearHeld();     // after depositing everything

}  // namespace EvacContext
