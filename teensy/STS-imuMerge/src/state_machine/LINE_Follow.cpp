#include "LINE_Follow.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/Touch.h"
#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"
#include "../actions/Turn.h"
#include "../actions/Forward.h"

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

            // After the timed U-turn, keep spinning with the same motor power
            // until XIAO's SearchLine mode sees the black line again.
            Actions::Drive::stop();
            Processing::XiaoDecode::setMode(XIAO_MODE_SEARCH_LINE);
            delay(200);
            Processing::XiaoDecode::clearFilter();

            while (Processing::XiaoDecode::command() != FEAT_SEARCH_LINE_BLACK) {
                Sensors::XIAO_link::tick();
                Processing::XiaoDecode::tick();
                Actions::Drive::motor(60.0f, -60.0f);
            }

            Actions::Drive::stop();
            Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
            delay(200);
            Processing::XiaoDecode::clearFilter();
            return;

        // Green turns: hardcoded straight-in then 90° spin (no continuous commit).
        case FEAT_GREEN_LEFT:
            #if PRINT_ACTIONS
                        Serial.println("Action: Green-Left");
            #endif
            Actions::Forward::forward(50.0f, 40.0f);
            Actions::Turn::turn(-90.0f, 60.0f);   // for left
            Actions::Drive::stop();
            Processing::XiaoDecode::clearFilter();
            return;

        case FEAT_GREEN_RIGHT:
            #if PRINT_ACTIONS
                        Serial.println("Action: Green-Right");
            #endif
            Actions::Forward::forward(50.0f, 40.0f);
            Actions::Turn::turn(90.0f, 60.0f);  // for right
            Actions::Drive::stop();
            Processing::XiaoDecode::clearFilter();
            return;

        case FEAT_RED:
            StateMachine::transitionTo(StateMachine::STALLED_RED);
            return;

        case FEAT_SILVER:
            Actions::Drive::stop();
            StateMachine::transitionTo(StateMachine::EVAC_ENTRY);
            return;

        // case FEAT_LINE_LOST:
        //     StateMachine::transitionTo(StateMachine::LINE_GAP);
        //     return;

        default:
            Actions::Drive::runLinePID();
            return;
    }
}

}  // namespace LINE_Follow
