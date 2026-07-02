#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/XIAO_link.h"
#include "../sensors/IMU.h"
#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"

#include <Arduino.h>
#include <math.h>

// =============================================================================
//  LINE_Gap - gap-crossing sequence.
//
//  XIAO runs MODE_LINE_ANGLE, which sends every frame:
//    ANGLE  - slope of the selected line points (127 = 0 deg)
//    FLAG   - bit0: at least one point, bit1: two or more points,
//             bit2: bottom edge point, bit4: side edge point
//    COM    - point Y, or average point Y when two points are available
//
//  This is intentionally one blocking chunk instead of a mini state machine.
// =============================================================================

namespace LINE_Gap {

namespace {
    constexpr float GAP_BACK_SPEED       = -50.0f;
    // constexpr uint16_t GAP_BACK_SETTLE_MS = 370;

    constexpr float GAP_ALIGN_KP         = 2.2f;
    constexpr float GAP_ALIGN_MIN_SPEED  = 12.0f;
    constexpr float GAP_ALIGN_MAX_SPEED  = 65.0f;
    constexpr float GAP_ALIGN_DEADBAND   = 2.0f;
    constexpr float GAP_GOAL_RIGHT_DOWN_DEG = 5.0f;
    constexpr float GAP_GOAL_LEFT_DOWN_DEG  = -5.0f;

    constexpr float GAP_FORWARD_SPEED    = 45.0f;
    constexpr float GAP_SIDE_ARC_INNER_SPEED = 30.0f;
    constexpr float GAP_SIDE_ARC_OUTER_SPEED = 45.0f;
    constexpr float GAP_SIDE_STRAIGHT_SPEED  = 50.0f;
    constexpr float GAP_SIDE_ARC_MS_PER_DEG  = 225.0f;
    constexpr uint16_t GAP_SIDE_ARC_MIN_MS   = 120;
    constexpr uint16_t GAP_SIDE_ARC_MAX_MS   = 700;
    constexpr uint16_t GAP_REVERSE_SETTLE_MS = 375;
    constexpr uint8_t GAP_BOTTOM_LOST_FRAMES  = 5;
    constexpr uint8_t GAP_BOTTOM_FOUND_FRAMES = 3;

    inline float signedAngleDeg() {
        return Processing::XiaoDecode::gapAngle() - 127.0f;
    }

    inline float signedFineAngleDeg() {
        return Processing::XiaoDecode::gapFineAngle() - 127.0f;
    }

    inline float alignmentAngleDeg() {
        return Processing::XiaoDecode::gapFineAngleFlag() ? signedFineAngleDeg()
                                                          : signedAngleDeg();
    }

    inline float gapGoalAngleDeg() {
        switch (Actions::Drive::lineFollowState()) {
            case Actions::Drive::LINE_FOLLOW_RIGHT_DOWN:
                return GAP_GOAL_RIGHT_DOWN_DEG;
            case Actions::Drive::LINE_FOLLOW_LEFT_DOWN:
                return GAP_GOAL_LEFT_DOWN_DEG;
            case Actions::Drive::LINE_FOLLOW_NOSE_UP:
            case Actions::Drive::LINE_FOLLOW_NOSE_DOWN:
            case Actions::Drive::LINE_FOLLOW_FLAT:
            default:
                return 0.0f;
        }
    }

    inline float gapAngleErrorDeg(float angle) {
        return angle - gapGoalAngleDeg();
    }

    inline bool sideDownGap() {
        const Actions::Drive::LineFollowState state = Actions::Drive::lineFollowState();
        return state == Actions::Drive::LINE_FOLLOW_LEFT_DOWN ||
               state == Actions::Drive::LINE_FOLLOW_RIGHT_DOWN;
    }

    inline float sideTiltDeg() {
        return -Sensors::IMU::getPitch();  // + = left side down for this IMU mount
    }

    inline float gapBackSpeed() {
        return sideDownGap() ? (GAP_BACK_SPEED + 8.0f) : GAP_BACK_SPEED;
    }

    inline void updateXiaoNow() {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
    }

    inline bool gapRedTriggered() {
        return Actions::Drive::lineFollowState() == Actions::Drive::LINE_FOLLOW_FLAT &&
               !StateMachine::redSuppressed() &&
               Sensors::XIAO_link::get(XIAO_REG_FEATURE) == FEAT_RED;
    }

    bool transitionToRedIfNeeded() {
        if (!gapRedTriggered()) return false;
        Actions::Drive::stop();
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::STALLED_RED);
        return true;
    }

    void pumpXiaoFor(uint16_t ms) {
        const unsigned long start = millis();
        while (millis() - start < ms) {
            delay(5);
            updateXiaoNow();
        }
    }

    inline bool gapBottomAndSideWhite() {
        return !Processing::XiaoDecode::gapBottomLineFlag() &&
               !Processing::XiaoDecode::gapSideLineFlag();
    }

    void driveForwardUntilBottomAndSideLostThenTwoPoints() {
        uint8_t lowFrames = 0;
        updateXiaoNow();
        while (lowFrames < GAP_BOTTOM_LOST_FRAMES) {
            Actions::Drive::motor(GAP_FORWARD_SPEED, GAP_FORWARD_SPEED);
            delay(5);
            updateXiaoNow();
            if (gapBottomAndSideWhite()) lowFrames++;
            else lowFrames = 0;
        }

        while (!Processing::XiaoDecode::gapBothRowsFlag()) {
            Actions::Drive::motor(GAP_FORWARD_SPEED, GAP_FORWARD_SPEED);
            delay(5);
            updateXiaoNow();
        }
        Actions::Drive::stop();
    }

    void driveSideArcThenStraightUntilBottomLostThenTwoPoints() {
        const bool leftDown =
            Actions::Drive::lineFollowState() == Actions::Drive::LINE_FOLLOW_LEFT_DOWN;
        const float arcLeft = leftDown ? GAP_SIDE_ARC_INNER_SPEED : GAP_SIDE_ARC_OUTER_SPEED;
        const float arcRight = leftDown ? GAP_SIDE_ARC_OUTER_SPEED : GAP_SIDE_ARC_INNER_SPEED;

        float arcMsFloat = fabsf(sideTiltDeg()) * GAP_SIDE_ARC_MS_PER_DEG;
        if (arcMsFloat < (float)GAP_SIDE_ARC_MIN_MS) arcMsFloat = (float)GAP_SIDE_ARC_MIN_MS;
        if (arcMsFloat > (float)GAP_SIDE_ARC_MAX_MS) arcMsFloat = (float)GAP_SIDE_ARC_MAX_MS;
        const uint16_t arcMs = (uint16_t)(arcMsFloat + 0.5f);

        bool bottomLost = false;
        uint8_t lowFrames = 0;
        uint8_t twoPointFrames = 0;
        const unsigned long arcStart = millis();

        while (true) {
            updateXiaoNow();

            if (!bottomLost) {
                if (Processing::XiaoDecode::gapBottomLineFlag()) lowFrames = 0;
                else if (++lowFrames >= GAP_BOTTOM_LOST_FRAMES) bottomLost = true;
            } else {
                if (Processing::XiaoDecode::gapBothRowsFlag()) {
                    if (++twoPointFrames >= GAP_BOTTOM_FOUND_FRAMES) break;
                } else {
                    twoPointFrames = 0;
                }
            }

            if (millis() - arcStart < arcMs) {
                Actions::Drive::motor(arcLeft, arcRight);
            } else {
                Actions::Drive::motor(GAP_SIDE_STRAIGHT_SPEED, GAP_SIDE_STRAIGHT_SPEED);
            }
            delay(5);
        }
        Actions::Drive::stop();
    }

    void alignToCurrentGapAngle() {
        updateXiaoNow();
        while (true) {
            const float angle = alignmentAngleDeg();
            const float error = gapAngleErrorDeg(angle);

#if PRINT_STATE
            Serial.print("GAP align angle: ");
            Serial.println(angle);
            Serial.print("GAP goal angle: ");
            Serial.println(gapGoalAngleDeg());
#endif

            if (error >= -GAP_ALIGN_DEADBAND && error <= GAP_ALIGN_DEADBAND) break;

            const float absAngle = fabsf(error);
            float power = error * GAP_ALIGN_KP;
            const float minPower = (absAngle < 15.0f) ? GAP_ALIGN_MIN_SPEED : (GAP_ALIGN_MIN_SPEED + 8.0f);
            if (power >  GAP_ALIGN_MAX_SPEED) power =  GAP_ALIGN_MAX_SPEED;
            if (power < -GAP_ALIGN_MAX_SPEED) power = -GAP_ALIGN_MAX_SPEED;
            if (power > 0.0f && power < minPower) power = minPower;
            if (power < 0.0f && power > -minPower) power = -minPower;

            // Positive angle means the line tilts right, so spin left-forward/right-back.
            Actions::Drive::motor(-power, power);
            delay(5);
            updateXiaoNow();
        }
        Actions::Drive::stop();
    }

    inline void returnToLineFollow() {
        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_GAP");
#endif
    Actions::Drive::stop();
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE_ANGLE);
    Processing::XiaoDecode::clearFilter();
    
    // tone(BUZZER_PIN, 7000, 100);
}

void update() {
    updateXiaoNow();
    if (transitionToRedIfNeeded()) return;

    const float backSpeed = gapBackSpeed();
    Actions::Drive::motor(backSpeed, backSpeed);
    while (sideDownGap() ? !Processing::XiaoDecode::gapAnyPointFlag()
                         : !Processing::XiaoDecode::gapBothRowsFlag()) {
        delay(5);
        updateXiaoNow();
        if (transitionToRedIfNeeded()) return;
    }

    Actions::Drive::stop();
    pumpXiaoFor(GAP_REVERSE_SETTLE_MS);
    Processing::XiaoDecode::clearFilter();

    if (sideDownGap()) {
        driveSideArcThenStraightUntilBottomLostThenTwoPoints();
        returnToLineFollow();
        return;
    }

    alignToCurrentGapAngle();
    driveForwardUntilBottomAndSideLostThenTwoPoints();
    returnToLineFollow();
}

}  // namespace LINE_Gap
