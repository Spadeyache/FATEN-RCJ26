#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/XIAO_link.h"
#include "../sensors/IMU.h"
#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"
#include "../actions/Forward.h"
#include "../actions/Turn.h"

#include <Arduino.h>
#include <math.h>

// =============================================================================
//  LINE_Gap - gap-crossing sequence.
//
//  XIAO runs MODE_LINE_ANGLE, which sends every frame:
//    ANGLE  - slope of the selected line points (127 = 0 deg)
//    FLAG   - bit0: at least one point, bit1: two or more points, bit2: bottom edge point
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
    constexpr float GAP_SAFE_FINE_DEG    = 45.0f;
    constexpr float GAP_GOAL_RIGHT_DOWN_DEG = 5.0f;
    constexpr float GAP_GOAL_LEFT_DOWN_DEG  = -5.0f;

    constexpr float GAP_FORWARD_SPEED    = 45.0f;
    constexpr float GAP_SIDE_ARC_INNER_SPEED = 30.0f;
    constexpr float GAP_SIDE_ARC_OUTER_SPEED = 50.0f;
    constexpr float GAP_SIDE_STRAIGHT_SPEED  = 50.0f;
    constexpr float GAP_SIDE_ARC_MS_PER_DEG  = 225.0f; //18
    constexpr uint16_t GAP_SIDE_ARC_MIN_MS   = 120;
    constexpr uint16_t GAP_SIDE_ARC_MAX_MS   = 700;
    constexpr float GAP_ROUGH_FWD_MM_PER_DEG = 1.3f;
    constexpr uint16_t GAP_ROUGH_BACK_BLIND_MS = 180;
    constexpr uint16_t GAP_REVERSE_SETTLE_MS = 80;
    constexpr uint16_t GAP_ANGLE_SETTLE_MS = 100;
    constexpr uint8_t GAP_BOTTOM_LOST_FRAMES  = 5;
    constexpr uint8_t GAP_BOTTOM_FOUND_FRAMES = 3;
    // constexpr uint16_t GAP_AFTER_LOST_BLIND_MS = 150;

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

    inline bool needsCurvedAcquire(float angle, bool fineOk) {
        return !fineOk || fabsf(gapAngleErrorDeg(angle)) >= GAP_SAFE_FINE_DEG;
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

    void driveForMs(float left, float right, uint16_t ms) {
        const unsigned long start = millis();
        while (millis() - start < ms) {
            Actions::Drive::motor(left, right);
            delay(5);
            updateXiaoNow();
        }
    }

    void pumpXiaoFor(uint16_t ms) {
        const unsigned long start = millis();
        while (millis() - start < ms) {
            delay(5);
            updateXiaoNow();
        }
    }

    void driveForwardUntilBottomLostThenFound() {
        uint8_t lowFrames = 0;
        updateXiaoNow();
        while (lowFrames < GAP_BOTTOM_LOST_FRAMES) {
            Actions::Drive::motor(GAP_FORWARD_SPEED, GAP_FORWARD_SPEED);
            delay(5);
            updateXiaoNow();
            if (Processing::XiaoDecode::gapBottomLineFlag()) lowFrames = 0;
            else lowFrames++;
        }

        // driveForMs(GAP_FORWARD_SPEED, GAP_FORWARD_SPEED, GAP_AFTER_LOST_BLIND_MS);
        pumpXiaoFor(150);

        uint8_t highFrames = 0;
        while (highFrames < GAP_BOTTOM_FOUND_FRAMES) {
            Actions::Drive::motor(GAP_FORWARD_SPEED, GAP_FORWARD_SPEED);
            delay(5);
            updateXiaoNow();
            if (Processing::XiaoDecode::gapBottomLineFlag()) highFrames++;
            else highFrames = 0;
        }
        Actions::Drive::stop();
    }

    void driveForwardUntilBottomLostThenTwoPoints() {
        uint8_t lowFrames = 0;
        updateXiaoNow();
        while (lowFrames < GAP_BOTTOM_LOST_FRAMES) {
            Actions::Drive::motor(GAP_FORWARD_SPEED, GAP_FORWARD_SPEED);
            delay(5);
            updateXiaoNow();
            if (Processing::XiaoDecode::gapBottomLineFlag()) lowFrames = 0;
            else lowFrames++;
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

    void backUntilAnyGapPoint() {
        const float backSpeed = gapBackSpeed();
        driveForMs(backSpeed, backSpeed, GAP_ROUGH_BACK_BLIND_MS);
        updateXiaoNow();
        while (!Processing::XiaoDecode::gapAnyPointFlag()) {
            Actions::Drive::motor(backSpeed, backSpeed);
            delay(5);
            updateXiaoNow();
        }
        Actions::Drive::stop();
    }

    void roughTurnBackToGapStart(float angleDeg) {
        const float fwdMm = fabsf(angleDeg) * GAP_ROUGH_FWD_MM_PER_DEG;
        Actions::Forward::forward(45.0f, fwdMm,
                                  /*useIMU=*/false, /*pumpComms=*/true);
        updateXiaoNow();
        Actions::Turn::turn(-angleDeg);
        Processing::XiaoDecode::clearFilter();
        backUntilAnyGapPoint();
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
    bool firstGapPass = true;
    while (true) {
        // Reverse until XIAO sees at least one usable line point.
        updateXiaoNow();
        const float backSpeed = gapBackSpeed();
        Actions::Drive::motor(backSpeed, backSpeed);
        while (!Processing::XiaoDecode::gapAnyPointFlag()) {
            delay(5);
            updateXiaoNow();
        }
        pumpXiaoFor(GAP_REVERSE_SETTLE_MS);
        
// Line lost detection
        updateXiaoNow();
        if (firstGapPass && Processing::XiaoDecode::gapBothRowsFlag()) {
            returnToLineFollow();
            // tone(BUZZER_PIN, 7000, 3000);
            return;
        }
        firstGapPass = false;

        // Step 1: single-point recovery. Save the angle/Y before blind motion changes the view.
        const bool savedFineOk = Processing::XiaoDecode::gapFineAngleFlag();
        const float savedAngle = savedFineOk ? signedFineAngleDeg() : signedAngleDeg();
        const float savedAngleError = gapAngleErrorDeg(savedAngle);

        
        Actions::Drive::stop();
        pumpXiaoFor(GAP_ANGLE_SETTLE_MS);


        Processing::XiaoDecode::clearFilter();

        if (sideDownGap()) {
            driveSideArcThenStraightUntilBottomLostThenTwoPoints();
            returnToLineFollow();
            return;
        }

        if (!needsCurvedAcquire(savedAngle, savedFineOk)) {     //small tile adjustment
            alignToCurrentGapAngle();
            driveForwardUntilBottomLostThenTwoPoints();

            returnToLineFollow();
            return;
        }
        roughTurnBackToGapStart(savedAngleError);               //tight turn adjustment

    }
}

}  // namespace LINE_Gap
