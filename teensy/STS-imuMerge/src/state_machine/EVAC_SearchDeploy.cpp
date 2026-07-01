#include "EVAC_SearchDeploy.h"
#include "StateMachine.h"
#include "VictimManager.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../actions/Drive.h"
#include "../actions/Arm.h"
#include "../actions/Forward.h"
#include "../actions/Turn.h"
#include "../sensors/Touch.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

namespace EVAC_SearchDeploy {

namespace {
    // --- EVAC_SearchDeploy-only tuning (moved out of config.h) -------------------
    // Approach (victim chase):
    constexpr float    EVAC_GRAB_BASE_SPEED       = 75.0f;
    constexpr float    EVAC_GRAB_TURN_GAIN        = 60.0f;
    constexpr float    CHASE_SPEED_FAR            = 65.0f;   // far (small box height) chase speed
    constexpr float    CHASE_SPEED_NEAR           = 45.0f;   // near (box at stop height) chase speed
    constexpr uint8_t  EVAC_GRAB_AVG_FRAMES       = 3;        // direction moving-average window
    constexpr uint8_t  EVAC_GRAB_LOST_HOLD_FRAMES = 5;        // coast this many frames when target drops
    constexpr uint8_t  EVAC_GRAB_STOP_WINDOW      = 5;        // over-height vote window
    constexpr uint8_t  EVAC_GRAB_STOP_REQUIRED    = 3;        // ...hits needed (3 of 5)
    // Overshoot guard: a tiny box hugging the frame bottom means the ball is too
    // close / under the camera. Back up until it returns to a grabbable view.
    constexpr int16_t  K230_FRAME_H               = 480;      // K230 sensor height (== SENSOR_H)
    constexpr int16_t  VICTIM_BACK_MAX_HEIGHT     = 60;       // box height <= this ...
    constexpr int16_t  VICTIM_BACK_MIN_Y2         = K230_FRAME_H - 65;  // ...and bottom y2 >= this (415) -> back up
    // Collection / deploy policy:
    constexpr uint32_t EVAC_SEARCH_TIMEOUT_MS     = 60000UL;  // 1-min search/deploy window
    constexpr uint32_t SPIN_SEARCH_MS             = 10000UL;  // spin in place this long with no victim, then roam forward
    constexpr uint32_t ROAM_FORWARD_MS            = 4000UL;   // forward-roam phase length between spins
    constexpr float    ROAM_TURN_SPEED            = 50.0f;    // turn speed used by the bump recovery
    constexpr float    EVAC_POINT_STOP_WIDTH_PX   = 300.0f;   // corner-approach stop width
    constexpr float    EVAC_POINT_STOP_HEIGHT_PX  = 140.0f;    // corner-approach stop height
    constexpr uint8_t  EVAC_POINT_STOP_REQUIRED   = 5;        // consecutive close frames at the corner
    constexpr int      EVAC_POINT_ALIGN_DEADBAND_PX = 30;     // centre band before colour read
    constexpr int      EVAC_POINT_ALIGN_SPEED_MIN   = 40;
    constexpr int      EVAC_POINT_ALIGN_SPEED_MAX   = 60;
    constexpr int      EVAC_POINT_ALIGN_MOVEMS_MIN  = 20;
    constexpr int      EVAC_POINT_ALIGN_MOVEMS_MAX  = 150;
    constexpr long     EVAC_POINT_ALIGN_MOVEMS_K    = 130;
    constexpr uint32_t EVAC_POINT_ALIGN_TIMEOUT_MS  = 2500;
    constexpr uint32_t EVAC_DEPLOY_TIMEOUT_MS     = 30000UL;  // give up hunting the corner after this
    constexpr uint32_t MODEL_SWAP_MS              = 1500;     // wait for the K230 to load a model
    constexpr int      DEPLOY_TOUCH_SPEED         = (int)EVAC_GRAB_BASE_SPEED;  // drive-into-corner speed = chase speed
    constexpr float    DEPLOY_TOUCH_TURN_GAIN     = 35.0f;    // keep corner centred while touching
    constexpr uint32_t DEPLOY_TOUCH_TIMEOUT_MS    = 4000;     // safety if the bumper never triggers
    constexpr uint32_t DEPLOY_TOUCH_CONFIRM_MS    = 50;       // ignore one-frame bumper noise
    constexpr int      DEPLOY_BACKOFF_SPEED       = -50;      // back off after release / wrong colour
    constexpr int      DEPLOY_BACKOFF_MM          = 100;

    uint32_t s_searchStartMs = 0;
    bool     s_disposeMode   = false;   // final drop-all after the timer: ignore the clock

    bool searchTimedOut() {
        if (s_disposeMode) return false;   // final disposal runs to completion
        return (uint32_t)(millis() - s_searchStartMs) >= EVAC_SEARCH_TIMEOUT_MS;
    }
    // (EVAC_GRAB_STOP_HEIGHT_PX stays in config.h — shared with VictimManager.)

    // --- direction / size helpers (image frame) ---
    float absF(float v) { return v < 0.0f ? -v : v; }

    float directionFor(const K230DBox& b) {
        const float cx = ((float)b.x1 + (float)b.x2) * 0.5f;
        float dir = (cx - K230_FRAME_CENTER_X) / K230_FRAME_CENTER_X;
        if (dir < -1.0f) dir = -1.0f;
        if (dir >  1.0f) dir =  1.0f;
        return dir;
    }
    float boxHeightPx(const K230DBox& b) { return absF((float)b.y2 - (float)b.y1); }
    float boxWidthPx(const K230DBox& b) { return absF((float)b.x2 - (float)b.x1); }
    int16_t boxCenterX(const K230DBox& b) { return (int16_t)(((int32_t)b.x1 + (int32_t)b.x2) / 2); }

    // Nearest-to-centre victim we still WANT (manager decides via acceptsType:
    // skips dead once one is held / the right arm is full).
    const K230DBox* closestCenterVictim() {
        const K230DBox* boxes = Processing::K230Decode::boxes();
        const uint8_t   n     = Processing::K230Decode::boxCount();
        const K230DBox* best  = nullptr;
        float bestAbs = 999.0f;
        for (uint8_t i = 0; i < n; i++) {
            if (!Processing::K230Decode::isVictimClass(boxes[i].cls)) continue;
            if (!VictimManager::acceptsType(boxes[i].cls)) continue;
            const float a = absF(directionFor(boxes[i]));
            if (best == nullptr || a < bestAbs) { best = &boxes[i]; bestAbs = a; }
        }
        return best;
    }

    // During a GREEN deploy run, opportunistically fill the spare right arm with
    // one more live ball, then resume corner hunting.
    const K230DBox* closestCenterLiveVictim() {
        const K230DBox* boxes = Processing::K230Decode::boxes();
        const uint8_t   n     = Processing::K230Decode::boxCount();
        const K230DBox* best  = nullptr;
        float bestAbs = 999.0f;
        for (uint8_t i = 0; i < n; i++) {
            if (boxes[i].cls != K230_CLASS_ALIVE) continue;
            if (!VictimManager::acceptsType(boxes[i].cls)) continue;
            const float a = absF(directionFor(boxes[i]));
            if (best == nullptr || a < bestAbs) { best = &boxes[i]; bestAbs = a; }
        }
        return best;
    }

    // Nearest-to-centre evac-point (corner) box.
    const K230DBox* closestCenterPoint() {
        const K230DBox* boxes = Processing::K230Decode::boxes();
        const uint8_t   n     = Processing::K230Decode::boxCount();
        const K230DBox* best  = nullptr;
        float bestAbs = 999.0f;
        for (uint8_t i = 0; i < n; i++) {
            if (!Processing::K230Decode::isPointClass(boxes[i].cls)) continue;
            const float a = absF(directionFor(boxes[i]));
            if (best == nullptr || a < bestAbs) { best = &boxes[i]; bestAbs = a; }
        }
        return best;
    }

    // Chase speed scales with distance: far (small box height) -> CHASE_SPEED_FAR,
    // slowing linearly to CHASE_SPEED_NEAR as the box height reaches stopHeightPx.
    float chaseSpeed(float heightPx, float stopHeightPx) {
        float t = heightPx / stopHeightPx;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return CHASE_SPEED_FAR + (CHASE_SPEED_NEAR - CHASE_SPEED_FAR) * t;
    }

    void driveTowardDirection(float dir, float baseSpeed) {
        const float turn  = dir * EVAC_GRAB_TURN_GAIN;
        Actions::Drive::motor(baseSpeed + turn, baseSpeed - turn);
    }

    // --- approach: drive at the nearest victim until close. ---
    // Returns the reached victim's class, or -1 if the victim was lost.
    int approachVictim(int onlyCls = -1) {
        uint8_t lost = 0;

        // Direction moving average (EVAC_GRAB_AVG_FRAMES) — smooths steering and
        // gives a sane value to coast on when a frame drops the target.
        float   dirWin[EVAC_GRAB_AVG_FRAMES] = {};
        uint8_t dirIdx = 0;
        uint8_t dirCnt = 0;
        float   smoothedDir = 0.0f;

        // Windowed stop vote: reach when EVAC_GRAB_STOP_REQUIRED of the last
        // EVAC_GRAB_STOP_WINDOW frames are over-height (smooths a flickery frame).
        bool    stopWin[EVAC_GRAB_STOP_WINDOW] = {};
        uint8_t stopIdx = 0;
        uint8_t stopCnt = 0;

        // Not gated by the search timer: a grab in progress runs to completion
        // (reach the ball, or lose it), and the deadline is handled by the caller.
        while (true) {
            Processing::K230Decode::tick();
            const K230DBox* t = (onlyCls == K230_CLASS_ALIVE) ? closestCenterLiveVictim()
                                                              : closestCenterVictim();

            if (t == nullptr) {
                if (lost++ >= EVAC_GRAB_LOST_HOLD_FRAMES) {
                    Actions::Drive::stop();
                    return -1;
                }
                driveTowardDirection(smoothedDir, CHASE_SPEED_FAR);   // coast fast on last dir
                delay(20);
                continue;
            }
            lost = 0;

            // Overshoot: tiny box at the very bottom -> ball too close / under the
            // camera. Back up until it comes back into a grabbable view.
            if (boxHeightPx(*t) <= VICTIM_BACK_MAX_HEIGHT && t->y2 >= VICTIM_BACK_MIN_Y2) {
                Actions::Drive::motor(-EVAC_GRAB_BASE_SPEED, -EVAC_GRAB_BASE_SPEED);
                delay(20);
                continue;
            }

            // stop vote: push this frame's over-height result into the ring.
            stopWin[stopIdx] = (boxHeightPx(*t) >= EVAC_GRAB_STOP_HEIGHT_PX);
            stopIdx = (stopIdx + 1) % EVAC_GRAB_STOP_WINDOW;
            if (stopCnt < EVAC_GRAB_STOP_WINDOW) stopCnt++;
            uint8_t hits = 0;
            for (uint8_t i = 0; i < stopCnt; i++) if (stopWin[i]) hits++;
            if (hits >= EVAC_GRAB_STOP_REQUIRED) {
                Actions::Drive::stop();
                return (int)t->cls;              // reached
            }

            // direction moving average -> steer on the smoothed value.
            dirWin[dirIdx] = directionFor(*t);
            dirIdx = (dirIdx + 1) % EVAC_GRAB_AVG_FRAMES;
            if (dirCnt < EVAC_GRAB_AVG_FRAMES) dirCnt++;
            float sum = 0.0f;
            for (uint8_t i = 0; i < dirCnt; i++) sum += dirWin[i];
            smoothedDir = sum / (float)dirCnt;

            driveTowardDirection(smoothedDir, chaseSpeed(boxHeightPx(*t), EVAC_GRAB_STOP_HEIGHT_PX));
            delay(20);
        }
        Actions::Drive::stop();
        return -1;
    }

    // After switching to the points model, vote the corner colour over a few
    // frames. Returns K230_POINT_GREEN / K230_POINT_RED, or -1 if undecided.
    int classifyPointColor() {
        int red = 0, green = 0;
        for (uint8_t i = 0; i < 8; i++) {
            if (searchTimedOut()) return -1;
            Processing::K230Decode::drainDelay(100);
            const int16_t c = Processing::K230Decode::dominantClass();
            if      (c == K230_POINT_RED)   red++;
            else if (c == K230_POINT_GREEN) green++;
        }
        if (red == 0 && green == 0) return -1;
        return (green >= red) ? K230_POINT_GREEN : K230_POINT_RED;
    }

    bool canGrabExtraLiveDuringGreenDeploy() {
        return VictimManager::count() == 2 &&
               VictimManager::liveHeld() == 2 &&
               VictimManager::acceptsType(K230_CLASS_ALIVE);
    }

    bool tryGrabExtraLiveDuringGreenDeploy() {
        if (searchTimedOut()) return false;
        if (!canGrabExtraLiveDuringGreenDeploy()) return false;
        if (closestCenterLiveVictim() == nullptr) return false;

        Serial.println("[deploy] extra live seen - grab before corner");
        const int type = approachVictim(K230_CLASS_ALIVE);
        if (type == K230_CLASS_ALIVE) {
            VictimManager::tryGrab(K230_CLASS_ALIVE);
            Actions::Drive::stop();
            const uint32_t seenPacketMs = Processing::K230Decode::lastPacketMs();
            Processing::K230Decode::waitForFreshFrameAfter(seenPacketMs, 700);
            return true;
        }
        return false;
    }

    // Spin + drive to the nearest POINT (victims model) until close. true if reached.
    bool approachPoint(bool allowExtraLiveGrab) {
        const uint32_t start = millis();
        uint8_t hits = 0;
        while (millis() - start < EVAC_DEPLOY_TIMEOUT_MS && !searchTimedOut()) {
            Processing::K230Decode::drainDelay(50);
            if (allowExtraLiveGrab && tryGrabExtraLiveDuringGreenDeploy()) {
                hits = 0;
                continue;
            }

            const K230DBox* corner = closestCenterPoint();
            if (corner == nullptr) { hits = 0; Actions::Drive::spinDecay(60, 400); continue; }
            if (boxWidthPx(*corner) > EVAC_POINT_STOP_WIDTH_PX &&
                boxHeightPx(*corner) >= EVAC_POINT_STOP_HEIGHT_PX) {
                if (++hits >= EVAC_POINT_STOP_REQUIRED) { Actions::Drive::stop(); return true; }
                Actions::Drive::stop();
            } else {
                hits = 0;
                driveTowardDirection(directionFor(*corner), chaseSpeed(boxHeightPx(*corner), EVAC_POINT_STOP_HEIGHT_PX));
            }
        }
        Actions::Drive::stop();
        return false;
    }

    bool alignPointToCenter(float* outDir = nullptr) {
        const uint32_t start = millis();
        while (millis() - start < EVAC_POINT_ALIGN_TIMEOUT_MS && !searchTimedOut()) {
            Processing::K230Decode::drainDelay(50);
            const K230DBox* corner = closestCenterPoint();
            if (corner == nullptr) {
                Actions::Drive::stop();
                Processing::K230Decode::drainDelay(100);
                continue;
            }

            const int delta = (int)boxCenterX(*corner) - (int)K230_FRAME_CENTER_X;
            if (outDir != nullptr) *outDir = directionFor(*corner);
            if (abs(delta) <= EVAC_POINT_ALIGN_DEADBAND_PX) {
                Actions::Drive::stop();
                return true;
            }

            const int absD   = abs(delta);
            const int speed  = constrain(map(absD, 8, 320, EVAC_POINT_ALIGN_SPEED_MIN, EVAC_POINT_ALIGN_SPEED_MAX),
                                         EVAC_POINT_ALIGN_SPEED_MIN, EVAC_POINT_ALIGN_SPEED_MAX);
            const int moveMs = constrain(EVAC_POINT_ALIGN_MOVEMS_MIN +
                                         (int)((EVAC_POINT_ALIGN_MOVEMS_K * absD * absD) / 102400L),
                                         EVAC_POINT_ALIGN_MOVEMS_MIN, EVAC_POINT_ALIGN_MOVEMS_MAX);
            const int dir = (delta < 0) ? -1 : 1;

            Actions::Drive::motor(dir * speed, -dir * speed);
            Processing::K230Decode::drainDelay(moveMs);
            Actions::Drive::stop();
        }
        return false;
    }

    // Swap to the points model, classify the corner colour, swap back to victims.
    int readCornerColor() {
        if (searchTimedOut()) return -1;
        Processing::K230Decode::setModel(Processing::K230Decode::MODEL_POINTS);
        Processing::K230Decode::drainDelay(MODEL_SWAP_MS);
        const int color = classifyPointColor();
        Processing::K230Decode::setModel(Processing::K230Decode::MODEL_VICTIMS);
        Processing::K230Decode::drainDelay(MODEL_SWAP_MS);
        return color;
    }

    // Drive forward until the front bumper hits, steering to keep the point centred.
    void driveToTouch(float startDir) {
        const uint32_t t0 = millis();
        float driveDir = startDir;
        while (millis() - t0 < DEPLOY_TOUCH_TIMEOUT_MS && !searchTimedOut()) {
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) {
                Actions::Drive::stop();
                Processing::K230Decode::drainDelay(DEPLOY_TOUCH_CONFIRM_MS);
                Sensors::Touch::tick();
                if (Sensors::Touch::front()) break;
            }

            Processing::K230Decode::tick();

            const K230DBox* corner = closestCenterPoint();
            if (corner != nullptr) driveDir = directionFor(*corner);

            const float turn = driveDir * DEPLOY_TOUCH_TURN_GAIN;
            Actions::Drive::motor(DEPLOY_TOUCH_SPEED + turn, DEPLOY_TOUCH_SPEED - turn);

            Processing::K230Decode::drainDelay(20);
        }
        Actions::Drive::stop();
    }

    // Find a corner of `targetColor` and drive into it. Wrong-colour corners are
    // backed away from and the search continues. Returns true once parked at one.
    bool driveToColorCorner(int targetColor) {
        const uint32_t start = millis();
        while (millis() - start < EVAC_DEPLOY_TIMEOUT_MS && !searchTimedOut()) {
            if (!approachPoint(targetColor == K230_POINT_GREEN)) return false;
            float pointDir = 0.0f;
            if (!alignPointToCenter(&pointDir)) continue;
            const int color = readCornerColor();
            Serial.printf("[deploy] corner=%s want=%s\n",
                          color == K230_POINT_GREEN ? "GREEN" : color == K230_POINT_RED ? "RED" : "?",
                          targetColor == K230_POINT_GREEN ? "GREEN" : "RED");
            if (color == targetColor) {
                if (!alignPointToCenter(&pointDir)) continue;
                driveToTouch(pointDir);
                if (searchTimedOut()) return false;
                Actions::Forward::forward(100, 80);
                return true;
            }
            // wrong corner -> back off + spin, look for another one.
            if (searchTimedOut()) return false;
            Actions::Forward::forward(-60, 50);
            if (searchTimedOut()) return false;
            Actions::Drive::spinDecay(70, 2000);
            if (searchTimedOut()) return false;
            Actions::Forward::forward(60, 200);
        }
        Actions::Drive::stop();
        return false;
    }

    // Deposit live balls at the GREEN corner, then back off.
    bool deployGreen() {
        Serial.println("[deploy] GREEN (live)");
        if (!driveToColorCorner(K230_POINT_GREEN) || searchTimedOut()) return false;
        VictimManager::releaseLive();
        Actions::Forward::forward(DEPLOY_BACKOFF_SPEED, DEPLOY_BACKOFF_MM);
        return true;
    }

    // Deposit the dead ball at the RED corner, then back off.
    bool deployRed() {
        Serial.println("[deploy] RED (dead)");
        if (!driveToColorCorner(K230_POINT_RED) || searchTimedOut()) return false;
        VictimManager::releaseDead();
        Actions::Forward::forward(DEPLOY_BACKOFF_SPEED, DEPLOY_BACKOFF_MM);
        return true;
    }

    // Drive forward `mm` at `speed` (both positive) while watching the front
    // bumper. On a hit: stop and turn the other way (turn(-50)).
    void forwardWatchTouch(int speed, int mm) {
        const unsigned long duration = (unsigned long)
            ((float)mm * FORWARD_MS_PER_MM * MAX_MOTOR_SPEED / (float)speed);
        const unsigned long startT = millis();
        Actions::Drive::motor(speed, speed);
        while (millis() - startT < duration && !searchTimedOut()) {
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) {
                Actions::Drive::stop();
                Actions::Turn::turn(-50.0f, ROAM_TURN_SPEED);
                return;
            }
        }
        Actions::Drive::stop();
    }

    // Front bumper hit while roaming: back off, turn away, nudge forward again.
    void bumpRecover() {
        if (searchTimedOut()) return;
        Serial.println("[search] bump -> recover");
        Actions::Drive::stop();
        Actions::Forward::forward(-60, 40);            // back off
        if (searchTimedOut()) return;
        Actions::Turn::turn(50.0f, ROAM_TURN_SPEED);   // turn away
        if (searchTimedOut()) return;
        forwardWatchTouch(60, 110);                    // forward; re-hit -> stop + turn(-50)
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_SEARCH_DEPLOY");
#endif
    VictimManager::reset();
    s_searchStartMs = millis();
}

void update() {
    uint32_t lastSeen = millis();   // last time a victim was in view (roam timer)

    // Collect-and-deploy loop. The 1-min timer never interrupts an in-progress
    // grab/approach; it is only checked here, between whole actions.
    while (true) {
        // Past the deadline: stop collecting. Finish disposing whatever we still
        // hold (live->green, dead->red, timer ignored), and only leave once both
        // are gone.
        if (searchTimedOut()) {
            if (VictimManager::liveHeld() > 0) {
                s_disposeMode = true; deployGreen(); s_disposeMode = false;
                continue;
            }
            if (VictimManager::deadHeld() > 0) {
                s_disposeMode = true; deployRed(); s_disposeMode = false;
                continue;
            }
            break;   // 1 min passed AND hands empty -> exit
        }

        // Deploy when we hold enough live (green trigger) OR we're already holding
        // both a live and a dead ball. Either way dispose EVERYTHING in hand:
        // live -> green corner, dead -> red corner.
        const bool holdsBoth =
            VictimManager::liveHeld() > 0 && VictimManager::deadHeld() > 0;
        if (VictimManager::readyToDeploy() || holdsBoth) {
            digitalWrite(LED_BUILTIN, HIGH);
            if (VictimManager::liveHeld() > 0) deployGreen();  // driveToColorCorner(GREEN)
            if (VictimManager::deadHeld() > 0) deployRed();    // driveToColorCorner(RED)
            lastSeen = millis();   // fresh search window after disposing
            continue;
        }

        Processing::K230Decode::drainDelay(50);   // always decide on a fresh frame

        // closestCenterVictim is already filtered to types we still want.
        if (closestCenterVictim() != nullptr) {
            lastSeen = millis();                 // reset roam timer: something in view
            const int type = approachVictim();   // runs to completion (reach or lose)
            if (type >= 0) {
                const bool grabbed = VictimManager::tryGrab((uint8_t)type);  // grab + self-confirm
                Actions::Drive::stop();
                Actions::Forward::forward(-50, 70);      // back up either way

                if (grabbed) {
                    // Booked: wait a fresh frame so the now-held ball isn't re-detected.
                    const uint32_t seenPacketMs = Processing::K230Decode::lastPacketMs();
                    Processing::K230Decode::waitForFreshFrameAfter(seenPacketMs, 700);
                }
                // On fail: count stays as-is (tryGrab didn't record). The loop then
                // re-detects the ball and goes back to approachVictim (moving in
                // front of it) instead of re-grabbing straight away.
            }
        } else {
            // Roam to find victims. A front-bumper hit at any moment triggers a
            // bump recovery. Otherwise: spin in place for SPIN_SEARCH_MS to sweep,
            // then roam forward for ROAM_FORWARD_MS, and repeat. lastSeen resets on
            // victim/deploy/recover, so it always restarts on the spin phase.
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) {
                bumpRecover();
                lastSeen = millis();                 // recovered -> restart spin timer
            } else {
                const uint32_t phase =
                    (millis() - lastSeen) % (SPIN_SEARCH_MS + ROAM_FORWARD_MS);
                if (phase < SPIN_SEARCH_MS) Actions::Drive::motor(50, -50);  // spin sweep
                else                        Actions::Drive::motor(60, 60);   // roam forward
            }
        }
    }

    // Loop only exits once timed out AND both hands are empty.
    Actions::Drive::stop();
    StateMachine::transitionTo(StateMachine::EVAC_EXIT);
}

}  // namespace EVAC_SearchDeploy
