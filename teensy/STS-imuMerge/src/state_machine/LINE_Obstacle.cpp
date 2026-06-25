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
//  LINE_Obstacle — front-bumper triggered avoidance + line re-acquisition.
//
//  1. Debounce, back off, turn -80° (pass the obstacle on the LEFT), nudge.
//  2. Go around: XIAO runs OBSTACLE mode; crawl in a curve (motor 100,7) with
//     conductivity micro-turns until the XIAO "see-line" flag (arc sees black).
//  3. Re-acquire the line:
//       a. Keep traversing OBS_TRAVERSE_AFTER_FLAG_MS so the line drops into the
//          tilted region of the box, then stop.
//       b. Settle OBS_SETTLE_MS and read the line tilt angle.
//       c. Back up OBS_BACK_MM, pre-spin OBS_PRESPIN_DEG, drive forward OBS_FWD_MM.
//       d. Spin up to OBS_SEARCH_SPIN_DEG searching for the see-line flag.
//            flag found  → restore LINE mode, hand back to LINE_FOLLOW.
//            not found   → spin back (OBS_SEARCH_SPIN_DEG − |angle|) the other
//                          way, then hand back to LINE_FOLLOW.
// =============================================================================

namespace LINE_Obstacle {

namespace {
    // Re-acquisition tuning.
    constexpr uint32_t OBS_TRAVERSE_AFTER_FLAG_MS = 1000;  // keep going around after first sight
    constexpr uint32_t OBS_SETTLE_MS              = 80;    // stand still before reading the angle
    constexpr float    OBS_BACK_MM               = 80.0f;
    constexpr float    OBS_PRESPIN_DEG           = 40.0f;
    constexpr float    OBS_FWD_MM                = 120.0f;
    constexpr float    OBS_SEARCH_SPIN_DEG       = 150.0f;
    constexpr float    OBS_SPIN_SPEED            = 60.0f;

    // Spin direction for a LEFT-side pass (motor 100,7). +1 = right (turn() sign).
    // Flip to -1.0f to mirror the whole re-acquisition sweep.
    constexpr float    OBS_SPIN_DIR              = 1.0f;

    // Pump the XIAO link + decoder for `ms`, keeping motors as last commanded.
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

    // Spin in `dirSign` (>0 = right) up to `maxDeg`, polling the see-line flag.
    // Returns true and stops immediately if the flag is raised; false if the
    // full angle elapsed without it. Duration scales like Actions::Turn::turn().
    bool spinSearchUntilFlag(float dirSign, float maxDeg, float speed) {
        const uint32_t duration =
            (uint32_t)(maxDeg * TURN_SPIN_MS_PER_DEG * MAX_MOTOR_SPEED / speed);
        const float l = (dirSign > 0) ?  speed : -speed;
        const float r = (dirSign > 0) ? -speed :  speed;
        Actions::Drive::motor(l, r);

        const uint32_t start = millis();
        uint32_t lastComms = 0;
        while (millis() - start < duration) {
            if (millis() - lastComms >= 20) {
                Sensors::XIAO_link::tick();
                Processing::XiaoDecode::tick(true);
                lastComms = millis();
                if (Processing::XiaoDecode::obstacleSeeLine()) {
                    Actions::Drive::stop();
                    return true;
                }
            }
        }
        Actions::Drive::stop();
        return false;
    }

    void finishToLineFollow() {
        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        delay(200);
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_OBSTACLE (touchfront)");
#endif
}

void update() {
    // 50 ms debounce before committing to the avoidance manoeuvre.
    delay(50);
    Sensors::Touch::tick();

    if (!Sensors::Touch::front()) {
        // False trigger — just resume following.
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
        return;
    }

    // ── 1. Back off, turn out, nudge forward ──────────────────────────────────
    Actions::Forward::forward(-70, 50);
    Actions::Turn::turn(-80.0f);

    Actions::Drive::stop();
    Processing::XiaoDecode::setMode(XIAO_MODE_OBSTACLE);
    delay(200);
    Processing::XiaoDecode::clearFilter();

    // ── 2. Go around the obstacle until the arc sees the line ─────────────────
    //  (body unchanged: curve forward + conductivity micro-turns; exit on flag)
    while (!Processing::XiaoDecode::obstacleSeeLine()) {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
        Sensors::Touch::tick();

        if (Sensors::Touch::conduct0()) {
            analogWrite(BUZZER_PIN, 80);
            Actions::Turn::turn(-20.0f);
            if (Processing::XiaoDecode::obstacleSeeLine()) break;
            Actions::Forward::forward(80, 2, /*useIMU=*/false, /*pumpComms=*/true);
        }
        analogWrite(BUZZER_PIN, 0);
        Actions::Drive::motor(100, 7);
    }
    analogWrite(BUZZER_PIN, 0);

    // ── 3a. Keep traversing so the line falls into the tilted region, then stop.
    const uint32_t start = millis();
    uint32_t lastComms = 0;
    while (millis() - start < OBS_TRAVERSE_AFTER_FLAG_MS) {
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            lastComms = millis();
        }
        Actions::Drive::motor(100, 7);
    }
    Actions::Drive::stop();

    // ── 3b. Settle and read the line tilt angle ───────────────────────────────
    pumpFor(OBS_SETTLE_MS);
    const float angleDeg = Processing::XiaoDecode::obstacleAngle() - 127.0f;  // signed
    const float angleMag = fabsf(angleDeg);
#if PRINT_STATE
    Serial.print("OBSTACLE re-acquire angle: "); Serial.println(angleDeg);
#endif

    // ── 3c. Reposition: back up, pre-spin, drive forward ──────────────────────
    Actions::Forward::forward(-70, OBS_BACK_MM);
    Actions::Turn::turn(OBS_PRESPIN_DEG * OBS_SPIN_DIR, OBS_SPIN_SPEED);
    Actions::Forward::forward(70, OBS_FWD_MM);

    // ── 3d. Search spin for the line ──────────────────────────────────────────
    if (spinSearchUntilFlag(OBS_SPIN_DIR, OBS_SEARCH_SPIN_DEG, OBS_SPIN_SPEED)) {
        finishToLineFollow();
        return;
    }

    // Not found on the full sweep → spin back (150 − |angle|) the other way.
    const float backDeg = OBS_SEARCH_SPIN_DEG - angleMag;
    if (backDeg > 0.0f) {
        Actions::Turn::turn(-backDeg * OBS_SPIN_DIR, OBS_SPIN_SPEED);
    }
    finishToLineFollow();
}

}  // namespace LINE_Obstacle
