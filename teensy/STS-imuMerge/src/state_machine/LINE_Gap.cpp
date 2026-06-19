#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"

#include <Arduino.h>

// =============================================================================
//  LINE_Gap â€” entered when the CommandFilter confirms FEAT_LINE_LOST.
//
//  Puts the XIAO into GAP mode (gap-angle reporting) and keeps driving the line
//  PID. Real gap-recovery (use Processing::XiaoDecode::gapAngle()) is still TODO
//  â€” for now this only wires the mode switch and exits when the line returns.
// =============================================================================

namespace LINE_Gap {

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_GAP");
#endif
    Processing::XiaoDecode::setMode(XIAO_MODE_GAP);
}

void update() {
    // Line reacquired → restore line mode and resume normal following.
    if (Processing::XiaoDecode::command() != FEAT_LINE_LOST) {
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
        return;
    }
    Actions::Drive::runLinePID();   // TODO: gap-angle guided recovery
}

}  // namespace LINE_Gap
