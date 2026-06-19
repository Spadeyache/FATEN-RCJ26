#include "LINE_Follow.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/Touch.h"
#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"
#include "../actions/Turn.h"

#include <Arduino.h>

// =============================================================================
//  LINE_Follow — default driving state.
//
//  Dispatch (highest priority first):
//    front bumper          → LINE_OBSTACLE
//    XIAO commit flag set  → FREEZE: only run the line PID (no transitions)
//    FEAT_UTURN (filtered) → 180° spin, stay in LINE_FOLLOW
//    FEAT_RED              → STALLED_RED
//    FEAT_SILVER           → EVAC_ENTRY
//    FEAT_LINE_LOST        → LINE_GAP
//    (none)                → runLinePID()
//
//  Green left/right turns are handled entirely on the XIAO (committed steering).
//  While a commit is in progress the XIAO raises XIAO_REG_FLAG; the Teensy then
//  freezes all state/command transitions and just follows the line error.
// =============================================================================

namespace LINE_Follow {

void onEnter() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#if PRINT_STATE
    Serial.println("State: LINE_FOLLOW");
#endif
}

void update() {
    // Front bumper has priority — hand off to the obstacle handler.
    if (Sensors::Touch::front()) {
        StateMachine::transitionTo(StateMachine::LINE_OBSTACLE);
        return;
    }

    // XIAO is mid green-turn → freeze every transition; just steer the error.
    if (Processing::XiaoDecode::commitFlag()) {
        Processing::XiaoDecode::clearFilter();   // don't accumulate stale votes
        Actions::Drive::runLinePID();
        return;
    }

    switch (Processing::XiaoDecode::command()) {
        case FEAT_UTURN:
#if PRINT_ACTIONS
            Serial.println("Action: U-Turn");
#endif
            Actions::Turn::turn(180.0f, 60.0f);
            Processing::XiaoDecode::clearFilter();
            return;

        // case FEAT_RED:
        //     StateMachine::transitionTo(StateMachine::STALLED_RED);
        //     return;

        // case FEAT_SILVER:
        //     Actions::Drive::stop();
        //     StateMachine::transitionTo(StateMachine::EVAC_ENTRY);
        //     return;

        // case FEAT_LINE_LOST:
        //     StateMachine::transitionTo(StateMachine::LINE_GAP);
        //     return;

        default:
            Actions::Drive::runLinePID();
            return;
    }
}

}  // namespace LINE_Follow
