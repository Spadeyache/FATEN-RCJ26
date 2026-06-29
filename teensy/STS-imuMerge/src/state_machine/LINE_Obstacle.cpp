#include "LINE_Obstacle.h"
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
#include <math.h>

// =============================================================================
//  LINE_Obstacle - front-bumper triggered avoidance + line re-acquisition.
//
//  1. Debounce, back off, turn +80 deg to pass the obstacle on the right.
//  2. Run XIAO LINE_ANGLE mode while curving around the obstacle.
//     End the first loop when LINE_ANGLE flag bit0 sees any point.
//  3. Keep traversing a little, stop, then reposition.
//  4. Drive forward until LINE_ANGLE sees any point again.
//     - two or more border points: return to LINE_FOLLOW
//     - one point: use the gap angle/Y rough recovery, then return
// =============================================================================

namespace LINE_Obstacle {

namespace {
    constexpr uint32_t OBS_TRAVERSE_AFTER_FLAG_MS = 1000;
    constexpr float    OBS_REACQUIRE_TURN_DEG     = 25.0f;
    constexpr float    OBS_REACQUIRE_TURN_SPEED   = 50.0f;
    constexpr uint16_t OBS_AFTER_POINT_EXTRA_MS   = 120;

    void pumpFor(uint32_t ms) {
        const uint32_t start = millis();
        uint32_t lastComms = 0;
        while (millis() - start < ms) {
            if (millis() - lastComms >= 20) {
                Sensors::XIAO_link::tick();
                Processing::XiaoDecode::tick(true);
                lastComms = millis();
            }
        }
    }

    void finishToLineFollow() {
        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        pumpFor(200);
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
    }

    inline float signedGapAngleDeg() {
        return Processing::XiaoDecode::gapFineAngleFlag()
            ? Processing::XiaoDecode::gapFineAngle() - 127.0f
            : Processing::XiaoDecode::gapAngle() - 127.0f;
    }

    void driveForwardUntilGapAnyPoint() {
        Actions::Drive::motor(40, 40);
        while (!Processing::XiaoDecode::gapAnyPointFlag()) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            Actions::Drive::motor(40, 40);
            delay(5);
        }
        pumpFor(OBS_AFTER_POINT_EXTRA_MS);
        Actions::Drive::stop();
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_OBSTACLE (touchfront)");
#endif
}

void update() {
    pumpFor(50);
    Sensors::Touch::tick();

    if (!Sensors::Touch::front()) {
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
        return;
    }

    Processing::XiaoDecode::setMode(XIAO_MODE_LINE_ANGLE);
    // pumpFor(200);
    Actions::Forward::forward(-60, 50, /*useIMU=*/false, /*pumpComms=*/true);
    Actions::Turn::turn(80.0f);
    
    Actions::Forward::forward(60, 50, /*useIMU=*/false, /*pumpComms=*/true);
    

    Processing::XiaoDecode::clearFilter();

    while (!Processing::XiaoDecode::gapAnyPointFlag()) {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
        Sensors::Touch::tick();

        if (Sensors::Touch::front()) {
            tone(BUZZER_PIN, 9000, 80);
            Actions::Turn::turn(20.0f);
            if (Processing::XiaoDecode::gapAnyPointFlag()) break;
            Actions::Forward::forward(60, 20, /*useIMU=*/false, /*pumpComms=*/true);
        }

        analogWrite(BUZZER_PIN, 0);
        Actions::Drive::motor(3, 67);
    }

    const uint32_t start = millis();
    uint32_t lastComms = 0;
    while (millis() - start < OBS_TRAVERSE_AFTER_FLAG_MS) {
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            lastComms = millis();
        }
        Actions::Drive::motor(3, 67);
    }
    tone(BUZZER_PIN, 1000, 500);

    Actions::Forward::forward(-60, 80, /*useIMU=*/false, /*pumpComms=*/true);
    Actions::Turn::turn(30, 60);
    driveForwardUntilGapAnyPoint();
    
    Actions::Drive::stop();
    delay(2500);

    if (Processing::XiaoDecode::gapBothRowsFlag()) {
        finishToLineFollow();
        return;
    }

    const float savedAngle = signedGapAngleDeg();
    const uint8_t savedY = Processing::XiaoDecode::gapLineY();
#if PRINT_STATE
    Serial.print("OBSTACLE one-point gap angle: ");
    Serial.println(savedAngle);
#endif

    Actions::Forward::forward(45.0f, (float)savedY * 0.25f,
                              /*useIMU=*/false, /*pumpComms=*/true);
    Actions::Turn::turn(savedAngle);
    finishToLineFollow();
}

}  // namespace LINE_Obstacle
