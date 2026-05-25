#include "LINE_Obstacle.h"
#include "StateMachine.h"
#include "config.h"

#include "../sensors/Touch.h"
#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"
#include "../actions/Turn.h"
#include "../actions/Forward.h"

#include <Arduino.h>

// =============================================================================
//  LINE_Obstacle — front-bumper triggered avoidance.
//
//  Sequence (preserved verbatim from the original FOLLOWING_LINE inline block):
//    1. Debounce 50 ms — confirm bumper still pressed
//    2. Back off 50 mm, turn -80°, nudge forward 2 mm
//    3. Switch XIAO into EVAC mode (looking for black line) and crawl
//       forward + conductivity-driven micro-turns until xiaoCommand reports
//       a black line (cmd 6 or 7).
//    4. Restore XIAO line-follow mode, drive forward 50 mm, hand back to
//       LINE_FOLLOW.
// =============================================================================

namespace LINE_Obstacle {

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_OBSTACLE (touchfront)");
#endif
}

void update() {
    // 50 ms debounce before committing to the avoidance manoeuvre.
    delay(50);
    Sensors::Touch::tick();

    if (Sensors::Touch::front()) {
        Actions::Forward::forward(-70, 50);
        Actions::Turn::turn(-80.0f);
        Actions::Forward::forward(70, 2);

        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_EVAC);
        delay(200);
        Processing::XiaoDecode::clearFilter();

        // Crawl forward; if conductivity probe sees the floor, micro-turn
        // and nudge until XIAO reports a black line (cmd 6 / 7).
        while (Processing::XiaoDecode::command() != 6
            && Processing::XiaoDecode::command() != 7) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick();
            Sensors::Touch::tick();

            if (Sensors::Touch::conduct0()) {
                analogWrite(BUZZER_PIN, 80);
                Actions::Turn::turn(-20.0f);
                if (Processing::XiaoDecode::command() == 6
                 || Processing::XiaoDecode::command() == 7) {
                    Processing::XiaoDecode::clearFilter();
                    break;
                }
                Actions::Forward::forward(80, 2, /*useIMU=*/false, /*pumpComms=*/true);
            }
            analogWrite(BUZZER_PIN, 0);
            Actions::Drive::motor(100, 7);
        }

        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        delay(200);
    }

    Actions::Forward::forward(70, 50);
    Processing::XiaoDecode::clearFilter();
    StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
}

}  // namespace LINE_Obstacle
