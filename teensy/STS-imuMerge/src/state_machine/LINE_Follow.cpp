#include "LINE_Follow.h"
#include "StateMachine.h"
#include "DeployPlan.h"
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
//    FEAT_LINE_LOST → gap #1 releases left, gap #2 releases right, stop 6 s, forward 50 mm,
//                            timed U-turn, then spin until center-point sees black
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
    uint8_t       _deployGapCount    = 0;

    // The very first left green of the run turns LEFT normally; the mirrored
    // deploy-return rule (turn right until both deploys passed) starts after.
    bool          _firstLeftGreenDone = false;

    bool bothDeployCountsPassed() {
        return _greenRightCount >= DeployPlan::leftDeployCount() &&
               _greenRightCount >= DeployPlan::rightDeployCount();
    }

    // One-shot green cooldown: after firing any green turn (u-turn/left/right)
    // we ignore all green for DISABLE_GREEN_MS so the same intersection isn't
    // re-read on the way out.
    bool          _disableGreen      = false;
    unsigned long _disableGreenStart = 0;
    unsigned long _disableGreenMs    = DISABLE_GREEN_MS;

    constexpr uint32_t DEPLOY_ARM_GRAB_HOLD_MS = 600;

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

    void runDeployGapSequence() {
        Actions::Drive::stop();
        ++_deployGapCount;
        const PendingArm gapArm =
            (_deployGapCount == 1) ? ARM_LEFT :
            (_deployGapCount == 2) ? ARM_RIGHT : ARM_NONE;
#if PRINT_ACTIONS
        Serial.printf("Action: Deploy gap #%u -> release %s arm\n",
                      _deployGapCount,
                      gapArm == ARM_LEFT ? "LEFT" : gapArm == ARM_RIGHT ? "RIGHT" : "NO");
#endif
        if (gapArm == ARM_LEFT) {
            Actions::Arm::liftDown();
            Actions::Arm::releaseLeft();
            pumpXiaoFor(DEPLOY_ARM_GRAB_HOLD_MS);
            Actions::Arm::liftCarry();
        } else if (gapArm == ARM_RIGHT) {
            Actions::Arm::liftDown();
            Actions::Arm::releaseRight();
            pumpXiaoFor(DEPLOY_ARM_GRAB_HOLD_MS);
            Actions::Arm::liftCarry();
        }
        _pendingArm = ARM_NONE;

        // Course-specific: a deploy gap while the right-green count sits at exactly
        // 3 counts as marker #4 (3 → 4 only; no other count is bumped here).
        if (_greenRightCount == 3) {
            ++_greenRightCount;
#if PRINT_ACTIONS
            Serial.println("Deploy gap: green-right count bumped 3 -> 4");
#endif
        }

        pumpXiaoFor(DEPLOY_TOUCH_STOP_MS);
        Actions::Forward::forward(LINE_FOLLOW_BASE_SPEED_FLAT, DEPLOY_GAP_FORWARD_MM,
                                  /*useIMU=*/false, /*pumpComms=*/true);
        Processing::XiaoDecode::setMode(XIAO_MODE_CENTER_POINT);
        pumpXiaoFor(60);
        Actions::Turn::turn(DEPLOY_UTURN_TIMED_DEG, DEPLOY_UTURN_SPEED);
        const unsigned long finishTimeoutMs =
            (unsigned long)(DEPLOY_CENTER_FINISH_MAX_DEG * TURN_SPIN_MS_PER_DEG *
                            MAX_MOTOR_SPEED / DEPLOY_UTURN_SPEED);
        const bool centered = Actions::Turn::turnUntilCenterPoint(1.0f,
                                                                  DEPLOY_UTURN_SPEED,
                                                                  finishTimeoutMs);
#if PRINT_ACTIONS
        Serial.printf("Deploy gap center finish: %s\n", centered ? "centered" : "timeout");
#endif
        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        Processing::XiaoDecode::clearFilter();
        armGreenCooldown();
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
            const bool deployLeft  = (_greenRightCount == DeployPlan::leftDeployCount());
            const bool deployRight = (_greenRightCount == DeployPlan::rightDeployCount());
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

        case FEAT_LINE_LOST:
            runDeployGapSequence();
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
