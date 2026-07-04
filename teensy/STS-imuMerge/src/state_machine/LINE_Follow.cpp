#include "LINE_Follow.h"
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
//  LINE_Follow — default driving state.
//
//  The XIAO sends the RAW per-frame feature byte; Processing::CommandFilter
//  here does all the voting/debouncing (see CommandFilter::update).
//
//  Dispatch (highest priority first):
//    front bumper          → LINE_OBSTACLE
//    FEAT_UTURN            → 180° spin + line re-acquire, stay in LINE_FOLLOW
//    FEAT_GREEN_LEFT       → counted; turn only on count LOW/HIGH, else forward
//    FEAT_GREEN_RIGHT      → hardcoded forward + 90° turn
//    FEAT_RED              → STALLED_RED
//    FEAT_SILVER           → EVAC_ENTRY
//    (none)                → runLinePID()
//
//  After any green turn (u-turn/left/right) a DISABLE_GREEN_MS cooldown ignores
//  all green so the same intersection isn't re-read on the way out.
// =============================================================================

namespace LINE_Follow {

namespace {
    struct IntersectionMotion {
        float forwardSpeed;
        float forwardMm;
        float turnAngle;
        float turnSpeed;
    };

    struct UTurnSequence {
        float preForwardSpeed;
        float preForwardMm;
        float firstTurnAngle;
        float firstTurnSpeed;
        float midForwardSpeed;
        float midForwardMm;
        float finalTurnAngle;
        float finalTurnSpeed;
        bool rawTurns;
    };

    // Left-green counter: each confirmed left green bumps this; the 90° turn
    // (and later the arm-deploy sequence) only fires when the count hits
    // GREEN_LEFT_TURN_COUNT_LOW or _HIGH. All other left greens are driven
    // straight through. Persists across state re-entries; reset at power-on.
    uint16_t      _greenLeftCount    = 0;

    // One-shot green cooldown: after firing any green turn (u-turn/left/right)
    // we ignore all green for DISABLE_GREEN_MS so the same intersection isn't
    // re-read on the way out.
    bool          _disableGreen      = false;
    unsigned long _disableGreenStart = 0;
    unsigned long _disableGreenMs    = DISABLE_GREEN_MS;

    void armGreenCooldown() {
        _disableGreen      = true;
        _disableGreenStart = millis();
        _disableGreenMs    = DISABLE_GREEN_MS;
    }

    void armBlackIntersectCooldown() {
        _disableGreen      = true;
        _disableGreenStart = millis();
        _disableGreenMs    = Actions::Drive::scaledLinePidMs(BLACK_INTERSECT_DISABLE_GREEN_BASE_MS,
                                                             BLACK_INTERSECT_DISABLE_GREEN_MIN_MS,
                                                             BLACK_INTERSECT_DISABLE_GREEN_MAX_MS);
    }

    void clearGreenIfElapsed() {
        if (_disableGreen && millis() - _disableGreenStart >= _disableGreenMs) {
            _disableGreen = false;
#if PRINT_STATE
            Serial.println("Green re-enabled");
#endif
        }
    }

    void pumpXiaoFor(uint32_t ms) {
        const uint32_t start = millis();
        while (millis() - start < ms) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick();
            Sensors::IMU::tick();
            delay(2);
        }
    }

    IntersectionMotion greenLeftMotion(Actions::Drive::LineFollowState state) {
        switch (state) {
            case Actions::Drive::LINE_FOLLOW_NOSE_UP:
                return {INTERSECTION_GREEN_NOSE_UP_FORWARD_SPEED,
                        INTERSECTION_GREEN_NOSE_UP_FORWARD_MM,
                        -INTERSECTION_GREEN_NOSE_UP_TURN_ANGLE,
                        INTERSECTION_GREEN_NOSE_UP_TURN_SPEED};
            case Actions::Drive::LINE_FOLLOW_NOSE_DOWN:
                return {INTERSECTION_GREEN_NOSE_DOWN_FORWARD_SPEED,
                        INTERSECTION_GREEN_NOSE_DOWN_FORWARD_MM,
                        -INTERSECTION_GREEN_NOSE_DOWN_TURN_ANGLE,
                        INTERSECTION_GREEN_NOSE_DOWN_TURN_SPEED};
            case Actions::Drive::LINE_FOLLOW_LEFT_DOWN:
                return {INTERSECTION_GREEN_SIDE_DOWNHILL_FORWARD_SPEED,
                        INTERSECTION_GREEN_SIDE_DOWNHILL_FORWARD_MM,
                        -INTERSECTION_GREEN_SIDE_DOWNHILL_TURN_ANGLE,
                        INTERSECTION_GREEN_SIDE_DOWNHILL_TURN_SPEED};
            case Actions::Drive::LINE_FOLLOW_RIGHT_DOWN:
                return {INTERSECTION_GREEN_SIDE_UPHILL_FORWARD_SPEED,
                        INTERSECTION_GREEN_SIDE_UPHILL_FORWARD_MM,
                        -INTERSECTION_GREEN_SIDE_UPHILL_TURN_ANGLE,
                        INTERSECTION_GREEN_SIDE_UPHILL_TURN_SPEED};
            case Actions::Drive::LINE_FOLLOW_FLAT:
            default:
                return {INTERSECTION_GREEN_LEFT_FLAT_FORWARD_SPEED,
                        INTERSECTION_GREEN_LEFT_FLAT_FORWARD_MM,
                        INTERSECTION_GREEN_LEFT_FLAT_TURN_ANGLE,
                        INTERSECTION_GREEN_LEFT_FLAT_TURN_SPEED};
        }
    }

    IntersectionMotion greenRightMotion(Actions::Drive::LineFollowState state) {
        switch (state) {
            case Actions::Drive::LINE_FOLLOW_NOSE_UP:
                return {INTERSECTION_GREEN_NOSE_UP_FORWARD_SPEED,
                        INTERSECTION_GREEN_NOSE_UP_FORWARD_MM,
                        INTERSECTION_GREEN_NOSE_UP_TURN_ANGLE,
                        INTERSECTION_GREEN_NOSE_UP_TURN_SPEED};
            case Actions::Drive::LINE_FOLLOW_NOSE_DOWN:
                return {INTERSECTION_GREEN_NOSE_DOWN_FORWARD_SPEED,
                        INTERSECTION_GREEN_NOSE_DOWN_FORWARD_MM,
                        INTERSECTION_GREEN_NOSE_DOWN_TURN_ANGLE,
                        INTERSECTION_GREEN_NOSE_DOWN_TURN_SPEED};
            case Actions::Drive::LINE_FOLLOW_LEFT_DOWN:
                return {INTERSECTION_GREEN_SIDE_UPHILL_FORWARD_SPEED,
                        INTERSECTION_GREEN_SIDE_UPHILL_FORWARD_MM,
                        INTERSECTION_GREEN_SIDE_UPHILL_TURN_ANGLE,
                        INTERSECTION_GREEN_SIDE_UPHILL_TURN_SPEED};
            case Actions::Drive::LINE_FOLLOW_RIGHT_DOWN:
                return {INTERSECTION_GREEN_SIDE_DOWNHILL_FORWARD_SPEED,
                        INTERSECTION_GREEN_SIDE_DOWNHILL_FORWARD_MM,
                        INTERSECTION_GREEN_SIDE_DOWNHILL_TURN_ANGLE,
                        INTERSECTION_GREEN_SIDE_DOWNHILL_TURN_SPEED};
            case Actions::Drive::LINE_FOLLOW_FLAT:
            default:
                return {INTERSECTION_GREEN_RIGHT_FLAT_FORWARD_SPEED,
                        INTERSECTION_GREEN_RIGHT_FLAT_FORWARD_MM,
                        INTERSECTION_GREEN_RIGHT_FLAT_TURN_ANGLE,
                        INTERSECTION_GREEN_RIGHT_FLAT_TURN_SPEED};
        }
    }

    UTurnSequence uturnSequence(Actions::Drive::LineFollowState state) {
        switch (state) {
            case Actions::Drive::LINE_FOLLOW_NOSE_UP:
                return {INTERSECTION_UTURN_NOSE_UP_PRE_FORWARD_SPEED,
                        INTERSECTION_UTURN_NOSE_UP_PRE_FORWARD_MM,
                        INTERSECTION_UTURN_NOSE_UP_FIRST_TURN_ANGLE,
                        INTERSECTION_UTURN_NOSE_UP_FIRST_TURN_SPEED,
                        INTERSECTION_UTURN_NOSE_UP_MID_FORWARD_SPEED,
                        INTERSECTION_UTURN_NOSE_UP_MID_FORWARD_MM,
                        INTERSECTION_UTURN_NOSE_UP_FINAL_TURN_ANGLE,
                        INTERSECTION_UTURN_NOSE_UP_FINAL_TURN_SPEED,
                        true};
            case Actions::Drive::LINE_FOLLOW_NOSE_DOWN:
                return {INTERSECTION_UTURN_NOSE_DOWN_PRE_FORWARD_SPEED,
                        INTERSECTION_UTURN_NOSE_DOWN_PRE_FORWARD_MM,
                        INTERSECTION_UTURN_NOSE_DOWN_FIRST_TURN_ANGLE,
                        INTERSECTION_UTURN_NOSE_DOWN_FIRST_TURN_SPEED,
                        INTERSECTION_UTURN_NOSE_DOWN_MID_FORWARD_SPEED,
                        INTERSECTION_UTURN_NOSE_DOWN_MID_FORWARD_MM,
                        INTERSECTION_UTURN_NOSE_DOWN_FINAL_TURN_ANGLE,
                        INTERSECTION_UTURN_NOSE_DOWN_FINAL_TURN_SPEED,
                        true};
            case Actions::Drive::LINE_FOLLOW_LEFT_DOWN:
                return {INTERSECTION_UTURN_LEFT_DOWN_PRE_FORWARD_SPEED,
                        INTERSECTION_UTURN_LEFT_DOWN_PRE_FORWARD_MM,
                        INTERSECTION_UTURN_LEFT_DOWN_FIRST_TURN_ANGLE,
                        INTERSECTION_UTURN_LEFT_DOWN_FIRST_TURN_SPEED,
                        INTERSECTION_UTURN_LEFT_DOWN_MID_FORWARD_SPEED,
                        INTERSECTION_UTURN_LEFT_DOWN_MID_FORWARD_MM,
                        INTERSECTION_UTURN_LEFT_DOWN_FINAL_TURN_ANGLE,
                        INTERSECTION_UTURN_LEFT_DOWN_FINAL_TURN_SPEED,
                        false};
            case Actions::Drive::LINE_FOLLOW_RIGHT_DOWN:
                return {INTERSECTION_UTURN_RIGHT_DOWN_PRE_FORWARD_SPEED,
                        INTERSECTION_UTURN_RIGHT_DOWN_PRE_FORWARD_MM,
                        INTERSECTION_UTURN_RIGHT_DOWN_FIRST_TURN_ANGLE,
                        INTERSECTION_UTURN_RIGHT_DOWN_FIRST_TURN_SPEED,
                        INTERSECTION_UTURN_RIGHT_DOWN_MID_FORWARD_SPEED,
                        INTERSECTION_UTURN_RIGHT_DOWN_MID_FORWARD_MM,
                        INTERSECTION_UTURN_RIGHT_DOWN_FINAL_TURN_ANGLE,
                        INTERSECTION_UTURN_RIGHT_DOWN_FINAL_TURN_SPEED,
                        false};
            case Actions::Drive::LINE_FOLLOW_FLAT:
            default:
                return {INTERSECTION_UTURN_FLAT_PRE_FORWARD_SPEED,
                        INTERSECTION_UTURN_FLAT_PRE_FORWARD_MM,
                        INTERSECTION_UTURN_FLAT_FIRST_TURN_ANGLE,
                        INTERSECTION_UTURN_FLAT_FIRST_TURN_SPEED,
                        INTERSECTION_UTURN_FLAT_MID_FORWARD_SPEED,
                        INTERSECTION_UTURN_FLAT_MID_FORWARD_MM,
                        INTERSECTION_UTURN_FLAT_FINAL_TURN_ANGLE,
                        INTERSECTION_UTURN_FLAT_FINAL_TURN_SPEED,
                        false};
        }
    }

    void runIntersectionMotion(const IntersectionMotion& motion) {
        if (motion.forwardSpeed != 0.0f && motion.forwardMm != 0.0f) {
            Actions::Forward::forward(motion.forwardSpeed, motion.forwardMm,
                                      /*useIMU=*/false, /*pumpComms=*/true);
        }

        Processing::XiaoDecode::setMode(XIAO_MODE_CENTER_POINT);
        pumpXiaoFor(60);

        const float sign = (motion.turnAngle >= 0.0f) ? 1.0f : -1.0f;
        const float absAngle = fabsf(motion.turnAngle);
        const float finishDeg = fminf(absAngle, INTERSECTION_GREEN_CENTER_FINISH_DEG);
        const float timedAngle = sign * (absAngle - finishDeg);
        if (timedAngle != 0.0f) {
            Actions::Turn::turn(timedAngle, motion.turnSpeed);
        }
        const unsigned long finishTimeoutMs =
            (unsigned long)(finishDeg * TURN_SPIN_MS_PER_DEG * MAX_MOTOR_SPEED / motion.turnSpeed);
        const bool centered = (finishTimeoutMs > 0)
            ? Actions::Turn::turnUntilCenterPoint(sign, motion.turnSpeed, finishTimeoutMs)
            : false;
#if PRINT_ACTIONS
        Serial.printf("Green center finish: %s (%.1f deg budget -> %lu ms)\n",
                      centered ? "centered" : "timeout", finishDeg, finishTimeoutMs);
#endif

        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
    }

    void runForwardIfNeeded(float speed, float mm) {
        if (speed != 0.0f && mm != 0.0f) {
            Actions::Forward::forward(speed, mm, /*useIMU=*/false, /*pumpComms=*/true);
        }
    }

    void runUTurnSequence(const UTurnSequence& seq) {
        runForwardIfNeeded(seq.preForwardSpeed, seq.preForwardMm);
        if (seq.rawTurns) Actions::Turn::turnRaw(seq.firstTurnAngle, seq.firstTurnSpeed);
        else              Actions::Turn::turn(seq.firstTurnAngle, seq.firstTurnSpeed);
        runForwardIfNeeded(seq.midForwardSpeed, seq.midForwardMm);
        if (seq.rawTurns) Actions::Turn::turnRaw(seq.finalTurnAngle, seq.finalTurnSpeed);
        else              Actions::Turn::turn(seq.finalTurnAngle, seq.finalTurnSpeed);
    }
}

void onEnter() {
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
    Processing::XiaoDecode::clearFilter();
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    _disableGreen = false;
#if PRINT_STATE
    Serial.println("State: LINE_FOLLOW");
#endif
}

void update() {
    clearGreenIfElapsed();

    // Front bumper has priority — hand off to the obstacle handler.
    if (Sensors::Touch::front()) {
        StateMachine::transitionTo(StateMachine::LINE_OBSTACLE);
        return;
    }

    switch (Processing::XiaoDecode::command()) {
        case FEAT_UTURN:
            if (_disableGreen) { Actions::Drive::runLinePID(); return; }
            {
            // Captured once, before the spin: held through the post-macro
            // suppression window so a mid-spin IMU blip can't be mistaken
            // for the slope state changing.
            const Actions::Drive::LineFollowState lfState = Actions::Drive::lineFollowState();
            const UTurnSequence seq = uturnSequence(lfState);
            #if PRINT_ACTIONS
                        Serial.printf("Action: U-Turn (%s)\n", Actions::Drive::lineFollowStateName(lfState));
            #endif
            runUTurnSequence(seq);

            // After the timed U-turn, keep spinning with the same motor power
            // until XIAO's SearchLine mode sees the black line again.
            Actions::Drive::stop();
            Processing::XiaoDecode::setMode(XIAO_MODE_SEARCH_LINE);
            pumpXiaoFor(200);
            Processing::XiaoDecode::clearFilter();

            while (Processing::XiaoDecode::command() != FEAT_SEARCH_LINE_BLACK) {
                Sensors::XIAO_link::tick();
                Processing::XiaoDecode::tick();
                Sensors::IMU::tick();
                Actions::Drive::motor(60.0f, -60.0f);
            }

            Actions::Drive::stop();
            Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
            pumpXiaoFor(200);
            Processing::XiaoDecode::clearFilter();
            armGreenCooldown();
            Actions::Drive::suppressSlopeDetection(lfState);
            }
            return;

        // Green left: counted. Only the GREEN_LEFT_TURN_COUNT_LOW-th and
        // _HIGH-th marker fire the turn (arm-deploy sequence goes there later);
        // every other left green is driven straight through.
        case FEAT_GREEN_LEFT:
            if (_disableGreen) { Actions::Drive::runLinePID(); return; }
            {
            const Actions::Drive::LineFollowState lfState = Actions::Drive::lineFollowState();
            const IntersectionMotion motion = greenLeftMotion(lfState);
            ++_greenLeftCount;
            const bool doTurn = (_greenLeftCount == GREEN_LEFT_TURN_COUNT_LOW ||
                                 _greenLeftCount == GREEN_LEFT_TURN_COUNT_HIGH);
            #if PRINT_ACTIONS
                        Serial.printf("Action: Green-Left #%u -> %s (%s)\n", _greenLeftCount,
                                      doTurn ? "TURN" : "forward",
                                      Actions::Drive::lineFollowStateName(lfState));
            #endif
            tone(BUZZER_PIN, 9000, 300);
            if (doTurn) {
                // TODO: deploy-arm sequence goes here.
                runIntersectionMotion(motion);
            } else {
                // Not a trigger count: drive straight through the intersection.
                runForwardIfNeeded(motion.forwardSpeed, motion.forwardMm);
            }
            Actions::Drive::stop();
            Processing::XiaoDecode::clearFilter();
            armGreenCooldown();
            Actions::Drive::suppressSlopeDetection(lfState);
            }
            return;

        case FEAT_GREEN_RIGHT:
            if (_disableGreen) { Actions::Drive::runLinePID(); return; }
            {
            const Actions::Drive::LineFollowState lfState = Actions::Drive::lineFollowState();
            const IntersectionMotion motion = greenRightMotion(lfState);
            #if PRINT_ACTIONS
                        Serial.printf("Action: Green-Right (%s)\n", Actions::Drive::lineFollowStateName(lfState));
            #endif
            tone(BUZZER_PIN, 9000, 300);
            runIntersectionMotion(motion);
            Actions::Drive::stop();
            Processing::XiaoDecode::clearFilter();
            armGreenCooldown();
            Actions::Drive::suppressSlopeDetection(lfState);
            }
            return;

        case FEAT_RED:
            if (StateMachine::redSuppressed()) {
                Processing::XiaoDecode::clearFilter();
                Actions::Drive::runLinePID();
                return;
            }
            StateMachine::transitionTo(StateMachine::STALLED_RED);
            return;

        case FEAT_SILVER:
            Actions::Drive::stop();
            StateMachine::transitionTo(StateMachine::EVAC_ENTRY);
            return;

        case FEAT_BLACK_INTERSECT:

            tone(BUZZER_PIN, 3000, 50);
            armBlackIntersectCooldown();
            Actions::Drive::suppressSlopeDetection(Actions::Drive::lineFollowState());
            Processing::XiaoDecode::clearFilter();
            Actions::Drive::runLinePID();
            return;

        case FEAT_LINE_LOST:
            StateMachine::transitionTo(StateMachine::LINE_GAP);
            return;

        default:
            Actions::Drive::runLinePID();
            return;
    }
}

}  // namespace LINE_Follow
