#include "EVAC_Deploy.h"
#include "StateMachine.h"
#include "EvacContext.h"
#include "../../config.h"

#include "../actions/Drive.h"
#include "../actions/Arm.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Deploy — drive to the evacuation corner and drop the held victims.
//
//  Flow:
//    - Spin until the K230 reports a POINT (corner) box.
//    - Track the corner closest to image centre, drive toward it.
//    - When the box is large enough (close), open the arms to release every
//      held ball, clear the held counts, and hand off to EVAC_EXIT.
//
//  Phase 1: every corner is treated as the (green/alive) drop point and ALL
//  balls are released here. Colour-based routing (alive→green, dead→red) is a
//  Phase 2 add-on once the colour model is loaded.
//
//  A safety timeout forces EVAC_EXIT if the corner is never reached, so a bad
//  frame or a missing corner can't hang the run.
// =============================================================================

namespace EVAC_Deploy {

namespace {
    unsigned long _enteredMs   = 0;
    uint8_t       _stopCount   = 0;   // consecutive close-enough frames

    float absF(float v) { return v < 0.0f ? -v : v; }

    float directionFor(const K230DBox &b) {
        const float cx = ((float)b.x1 + (float)b.x2) * 0.5f;
        float dir = (cx - K230_FRAME_CENTER_X) / K230_FRAME_CENTER_X;
        if (dir < -1.0f) dir = -1.0f;
        if (dir >  1.0f) dir =  1.0f;
        return dir;
    }

    float boxHeightPx(const K230DBox &b) {
        return absF((float)b.y2 - (float)b.y1);
    }

    // Corner box closest to image centre (most reliable to drive at).
    const K230DBox *closestCenterPoint() {
        const K230DBox *boxes = Processing::K230Decode::boxes();
        const uint8_t   count = Processing::K230Decode::boxCount();
        const K230DBox *best  = nullptr;
        float bestAbsDir = 999.0f;

        for (uint8_t i = 0; i < count; i++) {
            if (!Processing::K230Decode::isPointClass(boxes[i].cls)) continue;
            const float absDir = absF(directionFor(boxes[i]));
            if (best == nullptr || absDir < bestAbsDir) {
                best = &boxes[i];
                bestAbsDir = absDir;
            }
        }
        return best;
    }

    void driveTowardDirection(float direction) {
        const float turn  = direction * EVAC_GRAB_TURN_GAIN;
        const float left  = EVAC_GRAB_BASE_SPEED + turn;
        const float right = EVAC_GRAB_BASE_SPEED - turn;
        Actions::Drive::motor(left, right);
    }

    void deposit() {
        Actions::Drive::stop();
        Actions::Arm::releaseAll();      // Phase 1: drop everything here
        EvacContext::clearHeld();
#if PRINT_STATE
        Serial.println("EVAC_DEPLOY released all balls");
#endif
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_DEPLOY");
#endif
    _enteredMs = millis();
    _stopCount = 0;
}

void update() {
    // Safety: never get stuck hunting the corner.
    if (millis() - _enteredMs >= EVAC_DEPLOY_TIMEOUT_MS) {
#if PRINT_STATE
        Serial.println("EVAC_DEPLOY timeout -> EXIT");
#endif
        Actions::Drive::stop();
        StateMachine::transitionTo(StateMachine::EVAC_EXIT);
        return;
    }

    const K230DBox *corner = closestCenterPoint();

    if (corner == nullptr) {
        _stopCount = 0;
        Actions::Drive::motor(EVAC_SEARCH_SPIN_LEFT, EVAC_SEARCH_SPIN_RIGHT);
        return;
    }

    Processing::K230Decode::checkPoint();   // refresh pointPOS for logging/consumers

    if (boxHeightPx(*corner) >= EVAC_POINT_STOP_HEIGHT_PX) {
        if (++_stopCount >= EVAC_GRAB_STOP_REQUIRED) {
            deposit();
            StateMachine::transitionTo(StateMachine::EVAC_EXIT);
        } else {
            Actions::Drive::stop();
        }
        return;
    }

    _stopCount = 0;
    driveTowardDirection(directionFor(*corner));
}

}  // namespace EVAC_Deploy
