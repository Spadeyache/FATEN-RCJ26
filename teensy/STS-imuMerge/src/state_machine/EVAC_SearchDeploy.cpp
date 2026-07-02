#include "EVAC_SearchDeploy.h"
#include "StateMachine.h"
#include "VictimManager.h"
#include "EVAC_Entry.h"
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
    constexpr uint32_t EVAC_SEARCH_TIMEOUT_MS     = 105000UL; // 1m45s search/deploy window
    constexpr float    ROAM_TURN_SPEED            = 50.0f;    // turn speed used by the bump recovery
    constexpr uint32_t VICTIM_SEARCH_SPIN_MS      = 8000UL;   // spin this long for a victim before forward search
    constexpr uint32_t DEPLOY_POINT_SPIN_MS       = 8000UL;   // spin this long for a point before forward search
    constexpr uint32_t SEARCH_FORWARD_MS          = 5000UL;   // forward-search phase after a failed spin
    constexpr int      SEARCH_FORWARD_SPEED       = 60;
    constexpr int      TOUCH_RECOVER_BACK_MM      = 100;
    constexpr float    TOUCH_RECOVER_TURN_DEG     = 50.0f;
    constexpr float    EVAC_POINT_STOP_WIDTH_PX   = 450.0f;   // corner-approach stop width
    constexpr float    EVAC_POINT_STOP_HEIGHT_PX  = 140.0f;   // corner-approach stop height
    constexpr int16_t  EVAC_POINT_STOP_MIN_BOTTOM_Y_PX = 370; // corner bottom must be below this before colour read
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
    constexpr int      TIMEOUT_GREEN_TO_RED_BACK_SPEED = -55;
    constexpr int      TIMEOUT_GREEN_TO_RED_BACK_MM    = 60;
    constexpr float    TIMEOUT_GREEN_TO_RED_TURN_DEG   = 120.0f;
    constexpr uint8_t  POINT_COLOR_SCORE_MIN      = 77;       // 0..255, ~0.30 confidence
    constexpr uint8_t  POINT_RED_SCORE_MIN        = 179;      // 0..255, ~0.70 confidence
    constexpr uint8_t  POINT_COLOR_SAMPLE_FRAMES  = 5;
    constexpr int      POINT_COLOR_BACK_SPEED     = -45;
    constexpr int      POINT_COLOR_BACK_MM        = 50;

    uint32_t s_searchStartMs = 0;
    bool     s_disposeMode   = false;   // final drop-all after the timer: ignore the clock

    bool searchTimedOut() {
        if (s_disposeMode) return false;   // final disposal runs to completion
        // Global evac-wide clock (from EVAC_Entry::onEnter()) takes priority
        // over the local search/deploy window - whichever runs out first.
        if ((unsigned long)(millis() - EVAC_Entry::startMs()) >= EVAC_Entry::GLOBAL_TIMEOUT_MS) {
            return true;
        }
        return (uint32_t)(millis() - s_searchStartMs) >= EVAC_SEARCH_TIMEOUT_MS;
    }

    void signalDeployStart() {
        tone(BUZZER_PIN, 9000, 1000);
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

    bool pointClassSeen(uint8_t cls, uint8_t minScore) {
        const K230DBox* boxes = Processing::K230Decode::boxes();
        const uint8_t n = Processing::K230Decode::boxCount();
        for (uint8_t i = 0; i < n; i++) {
            if (boxes[i].cls == cls && boxes[i].score >= minScore) return true;
        }
        return false;
    }

    void samplePointColorWindow(uint8_t frames, bool& greenSeen, bool& redSeenHigh) {
        greenSeen = false;
        redSeenHigh = false;
        for (uint8_t i = 0; i < frames; i++) {
            if (searchTimedOut()) return;
            Processing::K230Decode::drainDelay(100);
            if (pointClassSeen(K230_POINT_GREEN, POINT_COLOR_SCORE_MIN)) greenSeen = true;
            if (pointClassSeen(K230_POINT_RED, POINT_RED_SCORE_MIN)) redSeenHigh = true;
        }
    }

    // Green has priority across two looks. Red only wins if both looks see a
    // high-confidence red point and neither look sees green.
    int classifyPointColor() {
        bool greenSeen = false;
        bool redFirstSeenHigh = false;
        samplePointColorWindow(POINT_COLOR_SAMPLE_FRAMES, greenSeen, redFirstSeenHigh);
        if (greenSeen) return K230_POINT_GREEN;
        if (searchTimedOut()) return -1;

        Actions::Forward::forward(POINT_COLOR_BACK_SPEED, POINT_COLOR_BACK_MM);
        bool redSecondSeenHigh = false;
        samplePointColorWindow(POINT_COLOR_SAMPLE_FRAMES, greenSeen, redSecondSeenHigh);
        if (greenSeen) return K230_POINT_GREEN;

        return (redFirstSeenHigh && redSecondSeenHigh) ? K230_POINT_RED : -1;
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

    bool deployTimedOut(uint32_t startMs) {
        return (uint32_t)(millis() - startMs) >= EVAC_DEPLOY_TIMEOUT_MS;
    }

    void touchRecover() {
        Actions::Drive::stop();
        Actions::Forward::forward(-SEARCH_FORWARD_SPEED, TOUCH_RECOVER_BACK_MM);
        if (searchTimedOut()) return;
        Actions::Turn::turn(TOUCH_RECOVER_TURN_DEG, ROAM_TURN_SPEED);
    }

    // Spin + drive to the nearest POINT (victims model) until close. true if reached.
    bool approachPoint(bool allowExtraLiveGrab) {
        const uint32_t start = millis();
        uint32_t noPointPhaseStart = 0;
        bool forwardSearch = false;
        uint8_t hits = 0;
        while (!deployTimedOut(start) && !searchTimedOut()) {
            Processing::K230Decode::drainDelay(50);
            if (allowExtraLiveGrab && tryGrabExtraLiveDuringGreenDeploy()) {
                hits = 0;
                noPointPhaseStart = 0;
                forwardSearch = false;
                continue;
            }

            const K230DBox* corner = closestCenterPoint();
            if (corner == nullptr) {
                hits = 0;
                Sensors::Touch::tick();
                if (Sensors::Touch::front()) {
                    Serial.println("[deploy] touch -> recover");
                    touchRecover();
                    noPointPhaseStart = 0;
                    forwardSearch = false;
                    continue;
                }

                if (noPointPhaseStart == 0) {
                    noPointPhaseStart = millis();
                    forwardSearch = false;
                }
                const uint32_t phaseMs = millis() - noPointPhaseStart;
                if (!forwardSearch && phaseMs >= DEPLOY_POINT_SPIN_MS) {
                    Serial.println("[deploy] no point after spin - forward search");
                    forwardSearch = true;
                    noPointPhaseStart = millis();
                } else if (forwardSearch && phaseMs >= SEARCH_FORWARD_MS) {
                    Serial.println("[deploy] no point after forward - spin search");
                    forwardSearch = false;
                    noPointPhaseStart = millis();
                }

                if (forwardSearch) {
                    Actions::Drive::motor(SEARCH_FORWARD_SPEED, SEARCH_FORWARD_SPEED);
                } else {
                    Actions::Drive::motor(60, -60);
                }
                continue;
            }
            noPointPhaseStart = 0;
            forwardSearch = false;
            if (boxWidthPx(*corner) > EVAC_POINT_STOP_WIDTH_PX &&
                boxHeightPx(*corner) >= EVAC_POINT_STOP_HEIGHT_PX &&
                corner->y2 > EVAC_POINT_STOP_MIN_BOTTOM_Y_PX) {
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

    bool shouldDropAllOnGreenDeploy(int targetColor) {
        return targetColor == K230_POINT_GREEN && VictimManager::count() >= 3;
    }

    // Find a corner of `targetColor` and drive into it. Wrong-colour corners are
    // backed away from and the search continues. Returns true once parked at one.
    bool driveToColorCorner(int targetColor) {
        const uint32_t start = millis();
        while (millis() - start < EVAC_DEPLOY_TIMEOUT_MS && !searchTimedOut()) {
            if (!approachPoint(targetColor == K230_POINT_GREEN)) return false;
            float pointDir = 0.0f;
            if (!alignPointToCenter(&pointDir)) continue;
            if (shouldDropAllOnGreenDeploy(targetColor)) {
                Serial.println("[deploy] GREEN full - drop all at this point");
                driveToTouch(pointDir);
                if (searchTimedOut()) return false;
                Actions::Forward::forward(100, 80);
                return true;
            }
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
        const bool dropAll = VictimManager::count() >= 3;
        VictimManager::releaseLive();
        if (dropAll && VictimManager::deadHeld() > 0) VictimManager::releaseDead();
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

}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_SEARCH_DEPLOY");
#endif
    VictimManager::reset();
    s_searchStartMs = millis();
}

void update() {
    uint32_t noVictimPhaseStart = 0;     // phase timer for spin/forward victim search
    bool     victimForwardSearch = false;
    bool     timeoutDeploySignaled = false;

    // Collect-and-deploy loop. The 1m45s timer never interrupts an in-progress
    // grab/approach; it is only checked here, between whole actions.
    while (true) {
        // Past the deadline: stop collecting. Finish disposing whatever we still
        // hold (live->green, dead->red, timer ignored), and only leave once both
        // are gone.
        if (searchTimedOut()) {
            const bool hasLive = VictimManager::liveHeld() > 0;
            const bool hasDead = VictimManager::deadHeld() > 0;
            if (hasLive || hasDead) {
                if (!timeoutDeploySignaled) {
                    signalDeployStart();
                    timeoutDeploySignaled = true;
                }
                s_disposeMode = true;
                bool greenDeployed = false;
                if (VictimManager::liveHeld() > 0) {
                    greenDeployed = deployGreen();
                }
                if (greenDeployed && VictimManager::deadHeld() > 0) {
                    Actions::Forward::forward(TIMEOUT_GREEN_TO_RED_BACK_SPEED,
                                              TIMEOUT_GREEN_TO_RED_BACK_MM);
                    Actions::Turn::turn(TIMEOUT_GREEN_TO_RED_TURN_DEG);
                }
                if (VictimManager::deadHeld() > 0) deployRed();
                s_disposeMode = false;
                continue;
            }
            break;   // 1m45s passed AND hands empty -> exit
        }

        // Deploy when we hold enough live (green trigger) OR we're already holding
        // both a live and a dead ball. Either way dispose EVERYTHING in hand:
        // live -> green corner, dead -> red corner.
        const bool holdsBoth =
            VictimManager::liveHeld() > 0 && VictimManager::deadHeld() > 0;
        if (VictimManager::readyToDeploy() || holdsBoth) {
            digitalWrite(LED_BUILTIN, HIGH);
            signalDeployStart();
            if (VictimManager::liveHeld() > 0) deployGreen();  // driveToColorCorner(GREEN)
            if (VictimManager::deadHeld() > 0) deployRed();    // driveToColorCorner(RED)
            noVictimPhaseStart = 0;  // fresh spin after disposing
            victimForwardSearch = false;
            continue;
        }

        Processing::K230Decode::drainDelay(50);   // always decide on a fresh frame

        // closestCenterVictim is already filtered to types we still want.
        if (closestCenterVictim() != nullptr) {
            noVictimPhaseStart = 0;              // reset search phase: something in view
            victimForwardSearch = false;
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
            // Search for victims after an attempt/loss: spin 8s, then drive
            // forward 5s. Both phases keep checking K230 and front touch.
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) {
                Serial.println("[search] touch -> recover");
                touchRecover();
                noVictimPhaseStart = 0;       // recovered -> restart spin phase
                victimForwardSearch = false;
                continue;
            }

            if (noVictimPhaseStart == 0) {
                noVictimPhaseStart = millis();
                victimForwardSearch = false;
            }

            const uint32_t phaseMs = millis() - noVictimPhaseStart;
            if (!victimForwardSearch && phaseMs >= VICTIM_SEARCH_SPIN_MS) {
                Serial.println("[search] no victim after spin - forward search");
                victimForwardSearch = true;
                noVictimPhaseStart = millis();
            } else if (victimForwardSearch && phaseMs >= SEARCH_FORWARD_MS) {
                Serial.println("[search] no victim after forward - spin search");
                victimForwardSearch = false;
                noVictimPhaseStart = millis();
            }

            if (victimForwardSearch) {
                Actions::Drive::motor(SEARCH_FORWARD_SPEED, SEARCH_FORWARD_SPEED);
            } else {
                Actions::Drive::motor(50, -50);
            }
        }
    }

    // Loop only exits once timed out AND both hands are empty.
    Actions::Drive::stop();
    StateMachine::transitionTo(StateMachine::EVAC_EXIT);
}

}  // namespace EVAC_SearchDeploy
