#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"
#include "../actions/Forward.h"

#include <Arduino.h>

namespace LINE_Gap {

namespace {
    enum GapPhase : uint8_t {
        GAP_DRIVE_TO_FRONT_LINE,
        GAP_ALIGN_TO_ANGLE,
        GAP_GO_FORWARD,
        GAP_SEARCH_LINE_END
    };

    GapPhase phase = GAP_DRIVE_TO_FRONT_LINE;

    constexpr float GAP_MOVE_SPEED = -45.0f;
    constexpr float GAP_SPIN_SPEED = 30.0f;
    constexpr float GAP_ANGLE_DEADBAND_DEG = 5.0f;

    inline float signedGapAngleDeg() {
        return Processing::XiaoDecode::gapAngle() - 127.0f;
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_GAP");
#endif
    phase = GAP_DRIVE_TO_FRONT_LINE;
    Processing::XiaoDecode::setMode(XIAO_MODE_GAP);
    Processing::XiaoDecode::clearFilter();
}

void update() {
    switch (phase) {
        case GAP_DRIVE_TO_FRONT_LINE:
            if (Processing::XiaoDecode::gapFrontFlag()) {
                delay(160);
                Actions::Drive::stop();
                delay(200);
                phase = GAP_ALIGN_TO_ANGLE;
                return;
            }
            Actions::Drive::motor(GAP_MOVE_SPEED, GAP_MOVE_SPEED);
            return;

        case GAP_ALIGN_TO_ANGLE: {
            
            const float angle = signedGapAngleDeg();
            Serial.println(angle);
            

            if (angle >= -GAP_ANGLE_DEADBAND_DEG && angle <= GAP_ANGLE_DEADBAND_DEG) {
                Actions::Drive::stop();
                Processing::XiaoDecode::setMode(XIAO_MODE_SEARCH_LINE);
                Processing::XiaoDecode::clearFilter();
                phase = GAP_GO_FORWARD;
                return;
            }

            if (angle > 0.0f) Actions::Drive::motor( GAP_SPIN_SPEED, -GAP_SPIN_SPEED);
            else              Actions::Drive::motor(-GAP_SPIN_SPEED, GAP_SPIN_SPEED);
            return;
        }
        case GAP_GO_FORWARD:
            Actions::Forward::forward(40, 65);
            phase = GAP_SEARCH_LINE_END;

        case GAP_SEARCH_LINE_END:
            
            if (Processing::XiaoDecode::command() == FEAT_SEARCH_LINE_BLACK) {
                Actions::Drive::stop();
                delay(3000);
                Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
                Processing::XiaoDecode::clearFilter();
                StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
                return;
            }
            Actions::Drive::motor(45, 45);
            return;
    }
}

}  // namespace LINE_Gap
