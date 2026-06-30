#include "LINE_Obstacle.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/Touch.h"
#include "../sensors/XIAO_link.h"
#include "../sensors/IMU.h"
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
    struct ObstacleEntryMotion {
        float backSpeed;
        float backMm;
        float turnAngle;
        float turnSpeed;
        float forwardSpeed;
        float forwardMm;
    };

    constexpr float    OBS_REACQUIRE_TURN_DEG     = 25.0f;
    constexpr float    OBS_REACQUIRE_TURN_SPEED   = 50.0f;
    constexpr uint16_t OBS_AFTER_POINT_EXTRA_MS   = 120;
    constexpr float    OBS_STATE_TILT_GATE_DEG    = 17.0f;
    constexpr float    OBS_ARC_ROTAXIS_PITCH_DEG  = 18.0f;
    constexpr float    OBS_TOUCH_ARC_LEFT          = 60.0f;
    constexpr float    OBS_TOUCH_ARC_RIGHT         = 5.0f;
    constexpr float    OBS_FREE_ARC_LEFT           = -3.0f;
    constexpr float    OBS_FREE_ARC_RIGHT          = 67.0f;

    constexpr ObstacleEntryMotion OBS_ENTRY_FLAT = {
        -55.0f, 40.0f,
         80.0f, 40.0f,
         55.0f, 10.0f
    };
    constexpr ObstacleEntryMotion OBS_ENTRY_NOSE_UP = {
        -55.0f, 40.0f,
         80.0f, 40.0f,
         55.0f, 10.0f
    };
    constexpr ObstacleEntryMotion OBS_ENTRY_NOSE_DOWN = {
        -55.0f, 40.0f,
         80.0f, 40.0f,
         55.0f, 10.0f
    };
    constexpr ObstacleEntryMotion OBS_ENTRY_LEFT_DOWN = {
        -55.0f, 40.0f,
         80.0f, 40.0f,
         55.0f, 10.0f
    };
    constexpr ObstacleEntryMotion OBS_ENTRY_RIGHT_DOWN = {
        -55.0f, 40.0f,
         80.0f, 40.0f,
         55.0f, 10.0f
    };

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

    void driveObstacleArc(float left, float right) {
        const float robotPitch = Sensors::IMU::getRoll();  // + = nose up for this IMU mount
        if (robotPitch > OBS_ARC_ROTAXIS_PITCH_DEG) {
            const float turnNorm = constrain((left - right) / (float)MAX_MOTOR_SPEED, -1.0f, 1.0f);
            Actions::Drive::motorSlopeProfiled(left, right, turnNorm);
        } else {
            Actions::Drive::motor(left, right);
        }
    }

    Actions::Drive::LineFollowState obstacleSlopeState() {
        const float pitch = Sensors::IMU::getRoll();    // + = nose up
        const float roll  = -Sensors::IMU::getPitch();  // + = left side down
        const float absPitch = fabsf(pitch);
        const float absRoll  = fabsf(roll);

        if (absPitch <= OBS_STATE_TILT_GATE_DEG && absRoll <= OBS_STATE_TILT_GATE_DEG)
            return Actions::Drive::LINE_FOLLOW_FLAT;

        if (absPitch >= absRoll)
            return (pitch > 0.0f) ? Actions::Drive::LINE_FOLLOW_NOSE_UP
                                  : Actions::Drive::LINE_FOLLOW_NOSE_DOWN;

        return (roll > 0.0f) ? Actions::Drive::LINE_FOLLOW_LEFT_DOWN
                             : Actions::Drive::LINE_FOLLOW_RIGHT_DOWN;
    }

    const ObstacleEntryMotion& obstacleEntryMotion(Actions::Drive::LineFollowState state) {
        switch (state) {
            case Actions::Drive::LINE_FOLLOW_NOSE_UP:    return OBS_ENTRY_NOSE_UP;
            case Actions::Drive::LINE_FOLLOW_NOSE_DOWN:  return OBS_ENTRY_NOSE_DOWN;
            case Actions::Drive::LINE_FOLLOW_LEFT_DOWN:  return OBS_ENTRY_LEFT_DOWN;
            case Actions::Drive::LINE_FOLLOW_RIGHT_DOWN: return OBS_ENTRY_RIGHT_DOWN;
            case Actions::Drive::LINE_FOLLOW_FLAT:
            default:                                     return OBS_ENTRY_FLAT;
        }
    }

    void runObstacleEntryMotion(const ObstacleEntryMotion& motion) {
        Actions::Forward::forward(motion.backSpeed, motion.backMm,
                                  /*useIMU=*/false, /*pumpComms=*/true);
        Actions::Turn::turn(motion.turnAngle, motion.turnSpeed);
        Actions::Forward::forward(motion.forwardSpeed, motion.forwardMm,
                                  /*useIMU=*/false, /*pumpComms=*/true);
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
    const Actions::Drive::LineFollowState obsState = obstacleSlopeState();
    runObstacleEntryMotion(obstacleEntryMotion(obsState));
    
    Processing::XiaoDecode::clearFilter();

    uint32_t start = millis();
    uint32_t lastComms = 0;
    while (millis() - start < 700) {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
        Sensors::Touch::tick();

        if (Sensors::Touch::front()) {
            tone(BUZZER_PIN, 9000, 80);
            driveObstacleArc(OBS_TOUCH_ARC_LEFT, OBS_TOUCH_ARC_RIGHT);
        }
        else{
            driveObstacleArc(OBS_FREE_ARC_LEFT, OBS_FREE_ARC_RIGHT);
        }
    }

    while (!Processing::XiaoDecode::gapAnyPointFlag()) {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
        Sensors::Touch::tick();

        if (Sensors::Touch::front()) {
            tone(BUZZER_PIN, 9000, 80);
            driveObstacleArc(OBS_TOUCH_ARC_LEFT, OBS_TOUCH_ARC_RIGHT);

            // Actions::Turn::turn(20.0f, 40.0f);
            // if (Processing::XiaoDecode::gapAnyPointFlag()) break;
            // Actions::Forward::forward(50, 20, /*useIMU=*/false, /*pumpComms=*/true);
        }
        else{
            driveObstacleArc(OBS_FREE_ARC_LEFT, OBS_FREE_ARC_RIGHT);
        }
    }

    start = millis();
    lastComms = 0;
    while (millis() - start < 700) {
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            lastComms = millis();
        }
        driveObstacleArc(OBS_FREE_ARC_LEFT, OBS_FREE_ARC_RIGHT);
    }
    tone(BUZZER_PIN, 1000, 500);

    Actions::Forward::forward(-50, 80, /*useIMU=*/false, /*pumpComms=*/true);
    Actions::Turn::turn(25, 45);
    driveForwardUntilGapAnyPoint();
    Actions::Drive::motor(50, 50);
    delay(790);
    Actions::Turn::turn(65, 45);

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
