#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/XIAO_link.h"
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

    constexpr float GAP_FORWARD_SPEED    = 45.0f;
    constexpr float GAP_ROUGH_FWD_MM_PER_DEG = 1.3f;
    constexpr uint16_t GAP_ROUGH_BACK_BLIND_MS = 180;
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

    inline bool needsCurvedAcquire(float angle, bool fineOk) {
        return !fineOk || fabsf(angle) >= GAP_SAFE_FINE_DEG;
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

    void alignToCurrentGapAngle() {
        updateXiaoNow();
        while (true) {
            const float angle = alignmentAngleDeg();

#if PRINT_STATE
            Serial.print("GAP align angle: ");
            Serial.println(angle);
#endif

            if (angle >= -GAP_ALIGN_DEADBAND && angle <= GAP_ALIGN_DEADBAND) break;

            const float absAngle = fabsf(angle);
            float power = angle * GAP_ALIGN_KP;
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
        driveForMs(GAP_BACK_SPEED, GAP_BACK_SPEED, GAP_ROUGH_BACK_BLIND_MS);
        updateXiaoNow();
        while (!Processing::XiaoDecode::gapAnyPointFlag()) {
            Actions::Drive::motor(GAP_BACK_SPEED, GAP_BACK_SPEED);
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
        Actions::Drive::motor(GAP_BACK_SPEED, GAP_BACK_SPEED);
        while (!Processing::XiaoDecode::gapAnyPointFlag()) {
            delay(5);
            updateXiaoNow();
        }
        // driveForMs(GAP_BACK_SPEED, GAP_BACK_SPEED, GAP_BACK_SETTLE_MS);
        pumpXiaoFor(375);//stop reverting to the line above
        
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

        
        Serial.println(savedAngle);
        Actions::Drive::stop();
        pumpXiaoFor(1500);


        Processing::XiaoDecode::clearFilter();

        if (!needsCurvedAcquire(savedAngle, savedFineOk)) {     //small tile adjustment
            alignToCurrentGapAngle();
            
            driveForwardUntilBottomLostThenTwoPoints();
            returnToLineFollow();
            return;
        }
        roughTurnBackToGapStart(savedAngle);                    //tight turn adjustment

    }
}

}  // namespace LINE_Gap
