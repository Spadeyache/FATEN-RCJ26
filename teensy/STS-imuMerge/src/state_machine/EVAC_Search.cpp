#include "EVAC_Search.h"
#include "StateMachine.h"
#include "EvacContext.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../actions/Drive.h"
#include "../actions/Arm.h"
// #include "../processing/Mapping.h"   // DISABLED — mapping off
#include "../processing/K230Decode.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Search â€” explore the zone, fill the map, detect victims/evac points.
//
//  Current flow:
//    - Spin in place while K230 has not found a victim.
//    - First victim detection uses K230Decode::checkVictim(), which keeps its
//      confidence-priority selection.
//    - grabBall() then tracks the victim closest to image center, smoothing the
//      steering direction with a short moving average.
// =============================================================================

namespace EVAC_Search {

namespace {
    bool          _initialised = false;
    bool          _grabbing = false;
    uint8_t       _grabType = K230_CLASS_ALIVE;   // class of the ball being grabbed
    uint8_t       _grabLostFrames = 0;
    uint8_t       _grabDirCount = 0;
    uint8_t       _grabDirIndex = 0;
    float32_t     _grabDirSamples[EVAC_GRAB_AVG_FRAMES] = {};
    float32_t     _grabSmoothedDir = 0.0f;
    bool          _grabStopSamples[EVAC_GRAB_STOP_WINDOW] = {};
    uint8_t       _grabStopCount = 0;
    uint8_t       _grabStopIndex = 0;
    unsigned long _lastBeep    = 0;

    float32_t absFloat(float32_t v) {
        return v < 0.0f ? -v : v;
    }

    float32_t directionFor(const K230DBox &b) {
        const float32_t centerX = ((float32_t)b.x1 + (float32_t)b.x2) * 0.5f;
        float32_t dir = (centerX - K230_FRAME_CENTER_X) / K230_FRAME_CENTER_X;
        if (dir < -1.0f) dir = -1.0f;
        if (dir >  1.0f) dir =  1.0f;
        return dir;
    }

    float32_t boxHeightPx(const K230DBox &b) {
        return absFloat((float32_t)b.y2 - (float32_t)b.y1);
    }

    const K230DBox *closestCenterVictim() {
        const K230DBox *boxes = Processing::K230Decode::boxes();
        const uint8_t count = Processing::K230Decode::boxCount();
        const K230DBox *best = nullptr;
        float32_t bestAbsDir = 999.0f;

        for (uint8_t i = 0; i < count; i++) {
            if (!Processing::K230Decode::isVictimClass(boxes[i].cls)) continue;

            const float32_t dir = directionFor(boxes[i]);
            const float32_t absDir = absFloat(dir);
            if (best == nullptr || absDir < bestAbsDir) {
                best = &boxes[i];
                bestAbsDir = absDir;
            }
        }
        return best;
    }

    void resetGrabFilter(float32_t initialDir) {
        _grabDirCount = EVAC_GRAB_AVG_FRAMES;
        _grabDirIndex = 0;
        _grabSmoothedDir = initialDir;
        _grabLostFrames = 0;
        for (uint8_t i = 0; i < EVAC_GRAB_AVG_FRAMES; i++) {
            _grabDirSamples[i] = initialDir;
        }
    }

    void resetGrabStopFilter() {
        _grabStopCount = 0;
        _grabStopIndex = 0;
        for (uint8_t i = 0; i < EVAC_GRAB_STOP_WINDOW; i++) {
            _grabStopSamples[i] = false;
        }
    }

    bool pushGrabStopSample(bool overHeight) {
        _grabStopSamples[_grabStopIndex] = overHeight;
        _grabStopIndex = (_grabStopIndex + 1) % EVAC_GRAB_STOP_WINDOW;
        if (_grabStopCount < EVAC_GRAB_STOP_WINDOW) _grabStopCount++;

        uint8_t hits = 0;
        for (uint8_t i = 0; i < _grabStopCount; i++) {
            if (_grabStopSamples[i]) hits++;
        }
        return hits >= EVAC_GRAB_STOP_REQUIRED;
    }

    float32_t pushGrabDirection(float32_t dir) {
        _grabDirSamples[_grabDirIndex] = dir;
        _grabDirIndex = (_grabDirIndex + 1) % EVAC_GRAB_AVG_FRAMES;
        if (_grabDirCount < EVAC_GRAB_AVG_FRAMES) _grabDirCount++;

        float32_t sum = 0.0f;
        for (uint8_t i = 0; i < _grabDirCount; i++) {
            sum += _grabDirSamples[i];
        }
        _grabSmoothedDir = sum / (float32_t)_grabDirCount;
        return _grabSmoothedDir;
    }

    void driveTowardDirection(float32_t direction) {
        const float32_t turn = direction * EVAC_GRAB_TURN_GAIN;
        const float32_t left = EVAC_GRAB_BASE_SPEED + turn;
        const float32_t right = EVAC_GRAB_BASE_SPEED - turn;
        Actions::Drive::motor(left, right);
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_SEARCH");
#endif
    // Processing::Mapping::init(/*restart=*/false);
    // Teensy-side K230 run/idle control disabled for now.
    // Processing::K230Decode::setRunning(true);
    _initialised = true;
    _grabbing = false;
    resetGrabFilter(0.0f);
    resetGrabStopFilter();
    _lastBeep    = 0;

    // 2-minute collection window starts now.
    EvacContext::startSearchTimer();
}

// Capture the reached victim with the matching arm and book it into the context.
static void captureVictim(uint8_t cls) {
    Actions::Drive::stop();
    if (cls == K230_CLASS_DEAD) {
        Actions::Arm::captureDead();
        EvacContext::addDead();
    } else {
        Actions::Arm::captureAlive();
        EvacContext::addAlive();
    }
#if PRINT_STATE
    Serial.printf("EVAC_SEARCH captured cls=%u  held=%u (dead=%u alive=%u)\n",
                  cls, EvacContext::heldTotal(),
                  EvacContext::heldDead(), EvacContext::heldAlive());
#endif
}

bool grabBall() {
    const K230DBox *target = closestCenterVictim();

    if (target != nullptr) {
        Processing::K230Decode::updateGoalFromVictim(*target);
        _grabType = target->cls;
        const float32_t height = boxHeightPx(*target);
        if (pushGrabStopSample(height >= EVAC_GRAB_STOP_HEIGHT_PX)) {
            // Reached the ball — grab it, then signal "done" so update() goes
            // back to spinning for the next victim.
            captureVictim(_grabType);
            resetGrabStopFilter();
            return false;
        }

        const float32_t smoothedDir = pushGrabDirection(directionFor(*target));

        Processing::K230Decode::goalPOS::direction = smoothedDir;
        Processing::K230Decode::goalPOS::updatedMs = millis();
        _grabLostFrames = 0;

        driveTowardDirection(smoothedDir);
        return true;
    }

    if (_grabDirCount > 0 && _grabLostFrames < EVAC_GRAB_LOST_HOLD_FRAMES) {
        _grabLostFrames++;
        driveTowardDirection(_grabSmoothedDir);
        return true;
    }

    Processing::K230Decode::goalPOS::valid = false;
    Actions::Drive::stop();
    return false;
}

void update() {
    if (!_initialised) {
        static unsigned long _warn = 0;
        if (millis() - _warn >= 1000) {
            Serial.println("EVAC_Search::update() called but _initialised=false");
            _warn = millis();
        }
        return;
    }

    // Processing::Mapping::tick();   // DISABLED — mapping off

    // Leave-collection decision: storage full, or the 2-minute window expired.
    if (EvacContext::full() || EvacContext::searchTimedOut()) {
        Actions::Drive::stop();
        if (EvacContext::heldTotal() > 0) {
            StateMachine::transitionTo(StateMachine::EVAC_DEPLOY);
        } else {
            // Timed out with nothing aboard — give up and leave the zone.
            StateMachine::transitionTo(StateMachine::EVAC_EXIT);
        }
        return;
    }

    if (!_grabbing) {
        if (Processing::K230Decode::checkVictim()) {
            _grabbing = true;
            resetGrabFilter(Processing::K230Decode::goalPOS::direction);
            resetGrabStopFilter();
#if PRINT_STATE
            Serial.printf("EVAC_SEARCH victim found dir=%.3f cls=%u score=%u\n",
                          Processing::K230Decode::goalPOS::direction,
                          Processing::K230Decode::goalPOS::cls,
                          Processing::K230Decode::goalPOS::score);
#endif
            _grabbing = grabBall();
        } else {
            Actions::Drive::motor(EVAC_SEARCH_SPIN_LEFT, EVAC_SEARCH_SPIN_RIGHT);
        }
    } else {
        if (!grabBall()) {
            _grabbing = false;
        }
    }

    // const unsigned long now = millis();
    // if (now - _lastBeep >= 1000) {
    //     // Longer beep so it's actually audible while debugging. Revert
    //     // to (160, 20ms) for the production short tick.
    //     analogWrite(BUZZER_PIN, 160); delay(120); analogWrite(BUZZER_PIN, 0);
    //     _lastBeep = now;
    // }
}

}  // namespace EVAC_Search
