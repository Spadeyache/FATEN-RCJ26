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
//  The XIAO sends the RAW per-frame feature byte; Processing::CommandFilter
//  here does all the voting/debouncing (see CommandFilter::update).
//
//  Dispatch (highest priority first):
//    front bumper          → LINE_OBSTACLE
//    FEAT_UTURN            → 180° spin + line re-acquire, stay in LINE_FOLLOW
//    FEAT_GREEN_LEFT/RIGHT → hardcoded forward + 90° turn
//    FEAT_RED              → STALLED_RED
//    FEAT_SILVER           → EVAC_ENTRY
//    (none)                → runLinePID()
//
//  After any green turn (u-turn/left/right) a DISABLE_GREEN_MS cooldown ignores
//  all green so the same intersection isn't re-read on the way out.
// =============================================================================

namespace LINE_Follow {

namespace {
    // One-shot green cooldown: after firing any green turn (u-turn/left/right)
    // we ignore all green for DISABLE_GREEN_MS so the same intersection isn't
    // re-read on the way out.
    bool          _disableGreen      = false;
    unsigned long _disableGreenStart = 0;

    void armGreenCooldown() {
        _disableGreen      = true;
        _disableGreenStart = millis();
    }

    void clearGreenIfElapsed() {
        if (_disableGreen && millis() - _disableGreenStart >= DISABLE_GREEN_MS) {
            _disableGreen = false;
#if PRINT_STATE
            Serial.println("Green re-enabled");
#endif
        }
    }
}

void onEnter() {
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
    Processing::XiaoDecode::clearFilter();
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    _disableGreen = false;
#if PRINT_STATE
    Serial.println("State: LINE_FOLLOW");
#endif
}

void update() {
    clearGreenIfElapsed();

    // Front bumper has priority — hand off to the obstacle handler.
    if (Sensors::Touch::front()) {
        StateMachine::transitionTo(StateMachine::LINE_OBSTACLE);
        return;
    }

    switch (Processing::XiaoDecode::command()) {
        case FEAT_UTURN:
            if (_disableGreen) { Actions::Drive::runLinePID(); return; }
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
            armGreenCooldown();
            return;

        // Green turns: hardcoded straight-in then 90° spin (no continuous commit).
        case FEAT_GREEN_LEFT:
            if (_disableGreen) { Actions::Drive::runLinePID(); return; }
            #if PRINT_ACTIONS
                        Serial.println("Action: Green-Left");
            #endif
            tone(BUZZER_PIN, 9000, 300);
            Actions::Forward::forward(50.0f, 52.0f);
            Actions::Turn::turn(-90.0f, 60.0f);   // for left
            Actions::Drive::stop();
            Processing::XiaoDecode::clearFilter();
            armGreenCooldown();
            return;

        case FEAT_GREEN_RIGHT:
            if (_disableGreen) { Actions::Drive::runLinePID(); return; }
            #if PRINT_ACTIONS
                        Serial.println("Action: Green-Right");
            #endif
            tone(BUZZER_PIN, 9000, 300);
            Actions::Forward::forward(50.0f, 52.0f);
            Actions::Turn::turn(90.0f, 60.0f);  // for right
            Actions::Drive::stop();
            Processing::XiaoDecode::clearFilter();
            armGreenCooldown();
            return;

        case FEAT_RED:
            StateMachine::transitionTo(StateMachine::STALLED_RED);
            return;

        case FEAT_SILVER:
            Actions::Drive::stop();
            StateMachine::transitionTo(StateMachine::EVAC_ENTRY);
            return;

        case FEAT_LINE_LOST:
            StateMachine::transitionTo(StateMachine::LINE_GAP);
            return;

        default:
            Actions::Drive::runLinePID();
            return;
    }
}

}  // namespace LINE_Follow
