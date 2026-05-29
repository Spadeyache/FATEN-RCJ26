#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"

#include <Arduino.h>

// =============================================================================
//  LINE_Gap â€” entered when CommandFilter reports cmd == 8 (line lost).
//
//  Current behaviour matches the original: fall through to PID line-follow.
//  In a future iteration this is where XIAO_MODE_GAP + gap-angle recovery
//  will live (use Processing::XiaoDecode::gapAngle()).
//
//  Exits back to LINE_FOLLOW as soon as cmd != 8.
// =============================================================================

namespace LINE_Gap {

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_GAP");
#endif
    // TODO: switch XIAO to GAP mode and use gapAngle() for recovery.
}

void update() {
    if (Processing::XiaoDecode::command() != 8) {
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
        return;
    }
    Actions::Drive::runLinePID();
}

}  // namespace LINE_Gap
