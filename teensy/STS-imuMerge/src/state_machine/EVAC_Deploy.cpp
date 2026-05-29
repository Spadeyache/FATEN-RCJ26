#include "EVAC_Deploy.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../actions/Drive.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Deploy â€” drop a victim at the correct evacuation corner.
//
//  SKELETON. Phase 2 work. Expected flow:
//    1. Read victim type (alive/dead) from the gripper state.
//    2. Pick target corner from the map (CS_EVAC_RED for dead,
//       CS_EVAC_GRN for alive).
//    3. Path-plan + drive there, lower arm, open gripper.
//    4. Back off, transition to EVAC_SEARCH (more victims) or EVAC_EXIT.
// =============================================================================

namespace EVAC_Deploy {

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_DEPLOY (stub)");
#endif
}

void update() {
    Actions::Drive::stop();
    // TODO phase 2: implement deploy sequence.
    // Placeholder: immediately hand back to search.
    StateMachine::transitionTo(StateMachine::EVAC_SEARCH);
}

}  // namespace EVAC_Deploy
