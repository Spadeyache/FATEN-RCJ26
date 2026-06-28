#include "EVAC_Entry.h"
#include "StateMachine.h"
#include "VictimManager.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/Touch.h"
#include "../sensors/IMU.h"
#include "../actions/Drive.h"
#include "../actions/Turn.h"
#include "../actions/Forward.h"
#include "../actions/Arm.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Entry â€” fixed entry sequence into the evacuation zone.
//
//  This is the verbatim sequence ported from the original enterEvacuationZone():
//    1. Re-engage grab servos, raise arm, open gripper
//    2. Drive in, turn, drive across the zone, drop gripper, lift away
//    3. Hunt for the wall: drive forward until touchfront
//    4. Beep, back off, drive in again until touchfront, turn-and-deposit
//
//  On completion: transitions to EVAC_SEARCH_DEPLOY.
//  All motions are blocking; this state runs once start-to-finish.
// =============================================================================

namespace EVAC_Entry {

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_ENTRY");
#endif

    // Fresh evac run: clear held counts.
    VictimManager::reset();
    digitalWrite(LED_BUILTIN, LOW);

    Actions::Arm::attachServos();

    Actions::Forward::forward(70, 170, /*useIMU=*/false, /*pumpComms=*/true);

    Actions::Drive::stop();
}

void update() {
    // onEnter() ran the entire entry sequence. Hand off to search+deploy.
    StateMachine::transitionTo(StateMachine::EVAC_SEARCH_DEPLOY);
}

}  // namespace EVAC_Entry
