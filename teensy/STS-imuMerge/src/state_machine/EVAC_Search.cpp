#include "EVAC_Search.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../actions/Drive.h"
#include "../processing/Mapping.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Search â€” explore the zone, fill the map, detect victims/evac points.
//
//  Phase 1 (current): passive â€” robot stays parked while Processing::Mapping
//  integrates ToF + IMU. 1 Hz heartbeat beep so the operator knows we're alive.
//
//  Phase 2 (TODO):
//    - Use Processing::Mapping::pose() and the occupancy grid to plan an
//      exploration path.
//    - Use Processing::K230Decode::detections() to localise victims and
//      evac corners.
//    - Transition to EVAC_DEPLOY once a victim is picked up.
// =============================================================================

namespace EVAC_Search {

namespace {
    bool          _initialised = false;
    unsigned long _lastBeep    = 0;
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_SEARCH");
#endif
    Processing::Mapping::init(/*restart=*/false);
    Processing::K230Decode::setRunning(true);
    _initialised = true;
    _lastBeep    = 0;
}

void update() {
    if (!_initialised) return;

    Actions::Drive::stop();          // phase 1: stay parked while map fills
    Processing::Mapping::tick();

    const unsigned long now = millis();
    if (now - _lastBeep >= 1000) {
        analogWrite(BUZZER_PIN, 160); delay(20); analogWrite(BUZZER_PIN, 0);
        _lastBeep = now;
    }

    // TODO phase 2:
    //   if (foundVictim) StateMachine::transitionTo(StateMachine::EVAC_DEPLOY);
}

}  // namespace EVAC_Search
