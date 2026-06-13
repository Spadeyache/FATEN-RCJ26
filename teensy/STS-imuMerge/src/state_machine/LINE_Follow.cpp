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
//  LINE_Follow â€” default driving state.
//
//  Priority dispatch (highest first):
//    touch front           â†’ LINE_OBSTACLE
//    xiaoCommand == 1      â†’ U-turn (inline blocking action, stay in LINE_FOLLOW)
//    xiaoCommand == 2 or 3 â†’ green turn (forward+turn, disableGreen cooldown)
//    xiaoCommand == 4      â†’ STALLED_RED
//    xiaoCommand == 5      â†’ EVAC_ENTRY
//    xiaoCommand == 6 or 7 â†’ no-green intersection (NGI) sub-sequence
//    xiaoCommand == 8      â†’ LINE_GAP
//    (none)                â†’ runLinePID()
//
//  disableGreen is a one-shot cooldown: green turns trigger it; after
//  DISABLE_GREEN_MS new green commands are accepted again.
// =============================================================================

namespace LINE_Follow {

namespace {
    bool          _disableGreen      = false;
    unsigned long _disableGreenStart = 0;

    void clearGreenIfElapsed() {
        if (_disableGreen && millis() - _disableGreenStart >= DISABLE_GREEN_MS) {
            _disableGreen = false;
            analogWrite(BUZZER_PIN, 0);
#if PRINT_STATE
            Serial.println("Green re-enabled");
#endif
        }
    }

    // NGI: stop, switch XIAO to NOGI mode, sample for 15 fast ticks, then act.
    void handleNoGreenIntersection() {
#if PRINT_XIAO
        const auto& f = Processing::XiaoDecode::filter();
        Serial.printf("CmdFilter | U:%u L:%u R:%u Red:%u Slv:%u Blk:%u | Cmd:%u Err:%.1f\n",
                      f.votesUturn, f.votesLeft, f.votesRight,
                      f.votesRed, f.votesSilver, f.votesBlack,
                      Processing::XiaoDecode::command(),
                      Processing::XiaoDecode::lineError());
#endif

        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_NOGI);
        Processing::XiaoDecode::clearFilter();
        delay(200);

        // Fast filter resamples (instantRun bypasses 20 ms throttle).
        for (int i = 0; i < 15; i++) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(/*instantRun=*/true);
        }

#if PRINT_XIAO
        const auto& f2 = Processing::XiaoDecode::filter();
        Serial.printf("CmdFilter | Blk:%u\n", f2.votesBlack);
#endif

        if (Processing::XiaoDecode::command() == 6) {
            Actions::Forward::forward(100, 40);
        }
        // (cmd == 7 fallthrough: future work â€” turn to the 90Â° direction)

        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        delay(200);
        Processing::XiaoDecode::clearFilter();
        Processing::XiaoDecode::setCommand(0);

        _disableGreen      = true;
        _disableGreenStart = millis();
        Actions::Drive::runLinePID();
    }
}

void onEnter() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#if PRINT_STATE
    Serial.println("State: LINE_FOLLOW");
#endif
}

void update() {
    clearGreenIfElapsed();

    // Front bumper has priority â€” hand off to the obstacle handler.
    if (Sensors::Touch::front()) {
        StateMachine::transitionTo(StateMachine::LINE_OBSTACLE);
        return;
    }

    const uint8_t cmd = Processing::XiaoDecode::command();

    // U-turn: 180° spin using the universal turn().
    if (cmd == 1) {
#if PRINT_ACTIONS
        Serial.println("Action: U-Turn");
#endif
        Actions::Turn::turn(180.0f, 60.0f);   // speed defaults to MAX_MOTOR_SPEED
        Processing::XiaoDecode::clearFilter();
        return;
    }

    // Green turns: short forward + 90Â° turn + cooldown.
    if (cmd == 2 && !_disableGreen) {
        Actions::Forward::forward(60, 35);
        Actions::Turn::turn(-75.0f, 60.0f);
        _disableGreen      = true;
        _disableGreenStart = millis();
        Processing::XiaoDecode::clearFilter();
        return;
    }
    if (cmd == 3 && !_disableGreen) {
        Actions::Forward::forward(60, 35);
        Actions::Turn::turn(75.0f, 60.0f);
        _disableGreen      = true;
        _disableGreenStart = millis();
        Processing::XiaoDecode::clearFilter();
        return;
    }

    if (cmd == 4) { StateMachine::transitionTo(StateMachine::STALLED_RED); return; }
    if (cmd == 5) { Actions::Drive::stop();
                    StateMachine::transitionTo(StateMachine::EVAC_ENTRY); return; }

    // NoGreenIntersection
    if ((cmd == 6 || cmd == 7) && !_disableGreen) {
        if (cmd == 7) {
            digitalWrite(LED_PIN, HIGH);
        }
        handleNoGreenIntersection();
        digitalWrite(LED_PIN, LOW);   // ensure LED off after NOGI
        return;
    }

    if (cmd == 8) { StateMachine::transitionTo(StateMachine::LINE_GAP); return; }

    // Default: just follow the line.
    Actions::Drive::runLinePID();
}

}  // namespace LINE_Follow
