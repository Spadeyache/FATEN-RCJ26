#include "LINE_Follow.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/XIAO_link.h"
#include "../sensors/Touch.h"
// #include "../sensors/IMU.h"  // IMU disabled — flat-only
#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"
#include "../actions/Turn.h"
#include "../actions/Forward.h"
#include "../actions/Arm.h"

#include <Arduino.h>
#include <math.h>

// =============================================================================
//  LINE_Follow — default driving state.
//
//  The XIAO sends the RAW per-frame feature byte; Processing::CommandFilter
//  here does all the voting/debouncing (see CommandFilter::update).
//
//  Dispatch (highest priority first):
//    front touch           → release pending arm, stop 6 s, 180° spin, resume
//    FEAT_GREEN_RIGHT      → counted; turn on count LEFT/RIGHT (sets which arm
//                            deploys at the wall); after both counts passed,
//                            the next right green = END → stop 12 s;
//                            otherwise drive straight through
//    FEAT_GREEN_LEFT       → both deploy counts passed → normal left turn;
//                            otherwise mirrored deploy-return → turn RIGHT
//    FEAT_SILVER           → EVAC
//    (none)                → runLinePID()
//
//  FLAT-ONLY: the IMU slope layer is disabled (see DEV_FORCE_TILT in Drive.cpp);
//  all intersection/u-turn motions use the FLAT tuning only.
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

    // Right-green counter: each confirmed right green bumps this; the deploy
    // turn fires when the count hits GREEN_RIGHT_TURN_COUNT_LEFT (→ left arm)
    // or _RIGHT (→ right arm). Persists across state re-entries; power-on reset.
    uint16_t      _greenRightCount   = 0;

    // Which arm the next front-touch (wall) releases; set by the deploy turn.
    enum PendingArm : uint8_t { ARM_NONE, ARM_LEFT, ARM_RIGHT };
    PendingArm    _pendingArm        = ARM_NONE;

    // The very first left green of the run turns LEFT normally; the mirrored
    // deploy-return rule (turn right until both deploys passed) starts after.
    bool          _firstLeftGreenDone = false;

    bool bothDeployCountsPassed() {
        return _greenRightCount >= GREEN_RIGHT_TURN_COUNT_LEFT &&
               _greenRightCount >= GREEN_RIGHT_TURN_COUNT_RIGHT;
    }

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
            // Sensors::IMU::tick();  // IMU disabled — flat-only
            delay(2);
        }
    }

    // Flat-only motions. The slope-aware (IMU LineFollowState) variants were
    // removed — see git history if slopes ever come back.
    constexpr IntersectionMotion GREEN_LEFT_MOTION = {
        INTERSECTION_GREEN_LEFT_FLAT_FORWARD_SPEED,
        INTERSECTION_GREEN_LEFT_FLAT_FORWARD_MM,
        INTERSECTION_GREEN_LEFT_FLAT_TURN_ANGLE,
        INTERSECTION_GREEN_LEFT_FLAT_TURN_SPEED};

    constexpr IntersectionMotion GREEN_RIGHT_MOTION = {
        INTERSECTION_GREEN_RIGHT_FLAT_FORWARD_SPEED,
        INTERSECTION_GREEN_RIGHT_FLAT_FORWARD_MM,
        INTERSECTION_GREEN_RIGHT_FLAT_TURN_ANGLE,
        INTERSECTION_GREEN_RIGHT_FLAT_TURN_SPEED};

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

    // Front touch = deploy wall: release the arm picked at the deploy turn
    // (nothing if no deploy is pending), stop DEPLOY_TOUCH_STOP_MS, spin 180°,
    // resume line following. Always active in this state.
    if (Sensors::Touch::front()) {
        Actions::Drive::stop();
#if PRINT_ACTIONS
        Serial.printf("Action: Deploy touch -> release %s arm\n",
                      _pendingArm == ARM_LEFT ? "LEFT" : _pendingArm == ARM_RIGHT ? "RIGHT" : "NO");
#endif
        if (_pendingArm == ARM_LEFT)       Actions::Arm::releaseLeft();
        else if (_pendingArm == ARM_RIGHT) Actions::Arm::releaseRight();
        _pendingArm = ARM_NONE;

        // Course-specific: a touch while the right-green count sits at exactly
        // 3 counts as marker #4 (3 → 4 only; no other count is bumped here).
        if (_greenRightCount == 3) {
            ++_greenRightCount;
#if PRINT_ACTIONS
            Serial.println("Deploy touch: green-right count bumped 3 -> 4");
#endif
        }

        pumpXiaoFor(DEPLOY_TOUCH_STOP_MS);
        Actions::Turn::turn(180.0f, DEPLOY_UTURN_SPEED);
        Actions::Drive::stop();
        Processing::XiaoDecode::clearFilter();
        armGreenCooldown();
        return;
    }

    switch (Processing::XiaoDecode::command()) {
        // Green right: counted. On count LEFT/RIGHT → deploy turn (remember
        // which arm to drop at the wall). After BOTH counts have been passed,
        // any further right green is the END marker: stop 12 s. Every other
        // right green is driven straight through.
        case FEAT_GREEN_RIGHT:
            if (_disableGreen) { Actions::Drive::runLinePID(); return; }
            {
            if (bothDeployCountsPassed()) {
                // End marker: full stop for END_RIGHT_GREEN_STOP_MS.
                #if PRINT_ACTIONS
                            Serial.println("Action: Green-Right END -> stop");
                #endif
                Actions::Drive::stop();
                tone(BUZZER_PIN, 9000, 300);
                pumpXiaoFor(END_RIGHT_GREEN_STOP_MS);
                Processing::XiaoDecode::clearFilter();
                armGreenCooldown();
                return;
            }

            ++_greenRightCount;
            const bool deployLeft  = (_greenRightCount == GREEN_RIGHT_TURN_COUNT_LEFT);
            const bool deployRight = (_greenRightCount == GREEN_RIGHT_TURN_COUNT_RIGHT);
            #if PRINT_ACTIONS
                        Serial.printf("Action: Green-Right #%u -> %s\n", _greenRightCount,
                                      deployLeft ? "TURN (left arm)"
                                                 : deployRight ? "TURN (right arm)" : "forward");
            #endif
            tone(BUZZER_PIN, 9000, 300);
            if (deployLeft || deployRight) {
                _pendingArm = deployLeft ? ARM_LEFT : ARM_RIGHT;
                runIntersectionMotion(GREEN_RIGHT_MOTION);
                Actions::Drive::stop();
                Processing::XiaoDecode::clearFilter();
                armGreenCooldown();
            } else {
                // Not a trigger count: drive straight through the intersection,
                // then ignore green for the same window as a saturated-black
                // intersection so the marker isn't re-read on the way out.
                runForwardIfNeeded(GREEN_RIGHT_MOTION.forwardSpeed, GREEN_RIGHT_MOTION.forwardMm);
                Actions::Drive::stop();
                Processing::XiaoDecode::clearFilter();
                armBlackIntersectCooldown();
            }
            }
            return;

        // Green left: the very first one of the run is a genuine marker →
        // normal left turn. After that: once both deploy counts are passed
        // it's genuine again → left; otherwise it's the deploy intersection
        // seen mirrored on the way back from the wall → turn RIGHT to resume.
        case FEAT_GREEN_LEFT:
            if (_disableGreen) { Actions::Drive::runLinePID(); return; }
            {
            const bool turnLeft = !_firstLeftGreenDone || bothDeployCountsPassed();
            _firstLeftGreenDone = true;
            #if PRINT_ACTIONS
                        Serial.printf("Action: Green-Left -> turn %s\n", turnLeft ? "LEFT" : "RIGHT");
            #endif
            tone(BUZZER_PIN, 9000, 300);
            runIntersectionMotion(turnLeft ? GREEN_LEFT_MOTION : GREEN_RIGHT_MOTION);
            Actions::Drive::stop();
            Processing::XiaoDecode::clearFilter();
            armGreenCooldown();
            }
            return;

        case FEAT_SILVER:
            Actions::Drive::stop();
            StateMachine::transitionTo(StateMachine::EVAC);
            return;

        case FEAT_BLACK_INTERSECT:

            tone(BUZZER_PIN, 3000, 50);
            armBlackIntersectCooldown();
            // Actions::Drive::suppressSlopeDetection(...);  // IMU disabled — flat-only
            Processing::XiaoDecode::clearFilter();
            Actions::Drive::runLinePID();
            return;

        default:
            Actions::Drive::runLinePID();
            return;
    }
}

}  // namespace LINE_Follow
