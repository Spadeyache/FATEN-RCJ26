#include "EVAC_Exit.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../actions/Drive.h"
#include "../processing/Mapping.h"
#include "../processing/K230Decode.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Exit â€” leave the evacuation zone via the entrance line.
//
//  SKELETON. Phase 2 work. Expected flow:
//    1. Persist the map to EEPROM (Processing::Mapping::persist()).
//    2. Plan a path back to the entrance (cell column 0, row MAP_ORIGIN_CY).
//    3. Drive out until XIAO sees the line again.
//    4. Switch XIAO back to LINE mode, clear filter, transition to LINE_FOLLOW.
// =============================================================================

namespace EVAC_Exit {

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_EXIT (stub)");
#endif
    Processing::Mapping::persist();
    Processing::K230Decode::setRunning(false);
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
}

void update() {
    Actions::Drive::stop();
    // TODO phase 2: implement exit sequence.
    // Placeholder: hand back to line-follow once the operator restarts.
    StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
}

}  // namespace EVAC_Exit
