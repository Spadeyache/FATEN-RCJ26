#include "EvacContext.h"
#include "../../config.h"

namespace EvacContext {

namespace {
    uint8_t       _heldDead     = 0;
    uint8_t       _heldAlive    = 0;
    unsigned long _searchStart  = 0;
    bool          _timerRunning = false;
}

void reset() {
    _heldDead     = 0;
    _heldAlive    = 0;
    _timerRunning = false;
}

void startSearchTimer() {
    _searchStart  = millis();
    _timerRunning = true;
}

bool searchTimedOut() {
    return _timerRunning &&
           (millis() - _searchStart) >= EVAC_SEARCH_TIMEOUT_MS;
}

uint8_t heldTotal() { return (uint8_t)(_heldDead + _heldAlive); }
uint8_t heldDead()  { return _heldDead; }
uint8_t heldAlive() { return _heldAlive; }

bool full() { return heldTotal() >= EVAC_MAX_BALLS; }

void addDead()  { if (heldTotal() < EVAC_MAX_BALLS) _heldDead++; }
void addAlive() { if (heldTotal() < EVAC_MAX_BALLS) _heldAlive++; }

void clearHeld() {
    _heldDead  = 0;
    _heldAlive = 0;
}

}  // namespace EvacContext
