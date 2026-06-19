#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"

#include <Arduino.h>

namespace LINE_Gap {

namespace {
    enum GapPhase : uint8_t {
        GAP_DRIVE_TO_FRONT_LINE,
        GAP_ALIGN_TO_ANGLE,
        GAP_SEARCH_LINE_END
    };

    GapPhase phase = GAP_DRIVE_TO_FRONT_LINE;

    constexpr float GAP_MOVE_SPEED = -45.0f;
    constexpr float GAP_SPIN_SPEED = 45.0f;
    constexpr float GAP_ANGLE_DEADBAND_DEG = 1.0f;

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
                Actions::Drive::stop();
                phase = GAP_ALIGN_TO_ANGLE;
                return;
            }
            Actions::Drive::motor(GAP_MOVE_SPEED, GAP_MOVE_SPEED);
            return;

        case GAP_ALIGN_TO_ANGLE: {
            const float angle = signedGapAngleDeg();
            if (angle >= -GAP_ANGLE_DEADBAND_DEG && angle <= GAP_ANGLE_DEADBAND_DEG) {
                Actions::Drive::stop();
                Processing::XiaoDecode::setMode(XIAO_MODE_SEARCH_LINE);
                Processing::XiaoDecode::clearFilter();
                phase = GAP_SEARCH_LINE_END;
                return;
            }

            if (angle > 0.0f) Actions::Drive::motor( GAP_SPIN_SPEED, -GAP_SPIN_SPEED);
            else              Actions::Drive::motor(-GAP_SPIN_SPEED,  GAP_SPIN_SPEED);
            return;
        }

        case GAP_SEARCH_LINE_END:
            if (Processing::XiaoDecode::command() == FEAT_SEARCH_LINE_BLACK) {
                Actions::Drive::stop();
                Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
                Processing::XiaoDecode::clearFilter();
                StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
                return;
            }
            Actions::Drive::motor(GAP_MOVE_SPEED, GAP_MOVE_SPEED);
            return;
    }
}

}  // namespace LINE_Gap
