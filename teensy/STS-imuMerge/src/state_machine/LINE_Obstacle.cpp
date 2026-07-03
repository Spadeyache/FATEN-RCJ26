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
    constexpr float    OBS_CENTER_FINISH_MAX_DEG  = 100.0f;  // give up the center-point spin past this
    constexpr float    OBS_REACQUIRE_TURN_SPEED   = 50.0f;
    constexpr uint16_t OBS_AFTER_POINT_EXTRA_MS   = 120;
    constexpr float    OBS_STATE_TILT_GATE_DEG    = 17.0f;
    constexpr float    OBS_ARC_ROTAXIS_PITCH_DEG  = 18.0f;
    constexpr float    OBS_SLOPE_ARC_SCALE         = 0.7f;   // slope: slow the go-around (anti-slide)
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
        -55.0f, 27.0f,   // back off DOWN the slope -> gravity assists, shorter (was 40)
         53.0f, 27.0f,   // uphill turn angle -> ~2/3 to avoid over-turning (was 80)
         45.0f, 20.0f     // post-turn straight: speed, distance(mm) - tune up from 0
    };
    constexpr ObstacleEntryMotion OBS_ENTRY_NOSE_DOWN = {
        -65.0f, 55.0f,   // back off UP the slope -> more distance, gravity eats it (was 40)
         60.0f, 30.0f,   // downhill turn angle -> 3/4 to avoid over-turning (was 80)
         35.0f, 12.0f     // post-turn straight: speed, distance(mm) - tune up from 0
    };
    constexpr ObstacleEntryMotion OBS_ENTRY_LEFT_DOWN = {
        -45.0f, 20.0f,
         53.0f, 38.0f,
         47.0f, 55.0f
    };
    constexpr ObstacleEntryMotion OBS_ENTRY_RIGHT_DOWN = {
        -50.0f, 25.0f,
         27.0f, 37.0f,
         45.0f, 25.0f
    };

    void pumpFor(uint32_t ms) {
        const uint32_t start = millis();
        uint32_t lastComms = 0;
        while (millis() - start < ms) {
            Sensors::IMU::tick();
            if (millis() - lastComms >= 20) {
                Sensors::XIAO_link::tick();
                Processing::XiaoDecode::tick(true);
                lastComms = millis();
            }
        }
    }

    void driveObstacleArc(float left, float right) {
        const float pitch = Sensors::IMU::getRoll();    // + = nose up for this IMU mount
        const float roll  = -Sensors::IMU::getPitch();  // + = left side down
        // Any tilt past the gate (up / down / sideways) routes through the
        // slope profile so downhill braking + upper-wheel de-rating apply too.
        if (fabsf(pitch) > OBS_ARC_ROTAXIS_PITCH_DEG || fabsf(roll) > OBS_ARC_ROTAXIS_PITCH_DEG) {
            left  *= OBS_SLOPE_ARC_SCALE;   // slow the go-around on slopes (anti-slide)
            right *= OBS_SLOPE_ARC_SCALE;
            const float turnNorm = constrain((left - right) / (float)MAX_MOTOR_SPEED, -1.0f, 1.0f);
            Actions::Drive::motorSlopeProfiled(left, right, turnNorm);
        } else {
            Actions::Drive::motor(left, right);
        }
    }

    Actions::Drive::LineFollowState obstacleSlopeState() {
        Sensors::IMU::tick();
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

    void printObstacleEntryMotion(Actions::Drive::LineFollowState state,
                                  const ObstacleEntryMotion& motion) {
#if PRINT_STATE
        const float pitch = Sensors::IMU::getRoll();
        const float roll  = -Sensors::IMU::getPitch();
        Serial.printf("Obstacle entry %s pitch:%.1f roll:%.1f back %.0f/%.0f turn %.0f/%.0f fwd %.0f/%.0f\n",
                      Actions::Drive::lineFollowStateName(state),
                      pitch, roll,
                      motion.backSpeed, motion.backMm,
                      motion.turnAngle, motion.turnSpeed,
                      motion.forwardSpeed, motion.forwardMm);
#endif
    }

    void finishToLineFollow() {
        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        pumpFor(200);
        Processing::XiaoDecode::clearFilter();
        // Hold whatever slope state was last tracked during the obstacle
        // traversal (continuously updated by motorSlopeProfiled) so the spin
        // back onto the line can't flip it via a transient IMU blip.
        Actions::Drive::suppressSlopeDetection(Actions::Drive::lineFollowState());
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
    }

    void driveForwardUntilGapAnyPoint() {
        Actions::Drive::motor(40, 40);
        while (!Processing::XiaoDecode::gapAnyPointFlag()) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            Sensors::IMU::tick();
            Actions::Drive::motor(40, 40);
            delay(5);
        }
        pumpFor(OBS_AFTER_POINT_EXTRA_MS);
        Actions::Drive::stop();
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
    }

    // Fallback when the center-point spin times out: creep forward in
    // LINE_ANGLE mode until either the top arc (front row) or the bottom
    // edge (near row) reports a line point.
    void driveForwardUntilLineRow() {
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE_ANGLE);
        pumpFor(60);
        tone(BUZZER_PIN, 3000, 100);
        Actions::Drive::motor(40, 40);
        while (!Processing::XiaoDecode::gapTopLineFlag() &&
               !Processing::XiaoDecode::gapBottomLineFlag()) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            Sensors::IMU::tick();
            Actions::Drive::motor(40, 40);
            delay(5);
        }
        Actions::Drive::stop();
#if PRINT_ACTIONS
        Serial.printf("Obstacle forward fallback: top=%d bottom=%d\n",
                      Processing::XiaoDecode::gapTopLineFlag() ? 1 : 0,
                      Processing::XiaoDecode::gapBottomLineFlag() ? 1 : 0);
#endif
    }

    bool turnWithCenterPointFinish(float angleDeg, float speed) {
        Processing::XiaoDecode::setMode(XIAO_MODE_CENTER_POINT);
        pumpFor(60);

        const float sign = (angleDeg >= 0.0f) ? 1.0f : -1.0f;
        const float absAngle = fabsf(angleDeg);
        const float finishDeg = fminf(absAngle, INTERSECTION_GREEN_CENTER_FINISH_DEG);
        const float timedAngle = sign * (absAngle - finishDeg);
        if (timedAngle != 0.0f) {
            Actions::Turn::turn(timedAngle, speed);
        }

        const bool centered = Actions::Turn::turnUntilCenterPointMaxDeg(
            sign, speed, OBS_CENTER_FINISH_MAX_DEG);
#if PRINT_ACTIONS
        Serial.printf("Obstacle center finish: %s (%.1f deg timed lead-in, max %.0f deg)\n",
                      centered ? "centered" : "timeout", finishDeg, OBS_CENTER_FINISH_MAX_DEG);
#endif
        return centered;
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
        Actions::Drive::suppressSlopeDetection(Actions::Drive::lineFollowState());
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
        return;
    }

    Processing::XiaoDecode::setMode(XIAO_MODE_LINE_ANGLE);
    // pumpFor(200);
    const Actions::Drive::LineFollowState obsState = obstacleSlopeState();
    const ObstacleEntryMotion& entryMotion = obstacleEntryMotion(obsState);
    printObstacleEntryMotion(obsState, entryMotion);
    runObstacleEntryMotion(entryMotion);
    
    Processing::XiaoDecode::clearFilter();

    uint32_t start = millis();
    uint32_t lastComms = 0;
    while (millis() - start < 700) {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
        Sensors::IMU::tick();
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
        Sensors::IMU::tick();
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
        Sensors::IMU::tick();
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            lastComms = millis();
        }
        driveObstacleArc(OBS_FREE_ARC_LEFT, OBS_FREE_ARC_RIGHT);
    }
    tone(BUZZER_PIN, 1000, 500);

    Actions::Forward::forward(50, 50, /*useIMU=*/false, /*pumpComms=*/true);
    // Actions::Forward::forward(-50, 80, /*useIMU=*/false, /*pumpComms=*/true);
    // Actions::Turn::turn(25, 45);
    // driveForwardUntilGapAnyPoint();
    // Actions::Drive::motor(50, 50);
    {
        // Replaces a blind delay(790) -- keeps the IMU filter fed through
        // this straight run instead of letting it go stale.
        const uint32_t holdStart = millis();
        while (millis() - holdStart < 790) {
            Sensors::IMU::tick();
        }
    }
    const bool centered = turnWithCenterPointFinish(65.0f, 45.0f);
    if (!centered) {
        driveForwardUntilLineRow();
    }
    finishToLineFollow();
    return;
}

}  // namespace LINE_Obstacle
