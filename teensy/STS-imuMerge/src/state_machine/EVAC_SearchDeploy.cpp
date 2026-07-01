#include "EVAC_SearchDeploy.h"
#include "StateMachine.h"
#include "VictimManager.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../actions/Drive.h"
#include "../actions/Arm.h"
#include "../actions/Forward.h"
#include "../sensors/Touch.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

namespace EVAC_SearchDeploy {

namespace {
    // --- EVAC_SearchDeploy-only tuning (moved out of config.h) -------------------
    // Approach (victim chase):
    constexpr float    EVAC_GRAB_BASE_SPEED       = 45.0f;
    constexpr float    EVAC_GRAB_TURN_GAIN        = 35.0f;
    constexpr uint8_t  EVAC_GRAB_AVG_FRAMES       = 3;        // direction moving-average window
    constexpr uint8_t  EVAC_GRAB_LOST_HOLD_FRAMES = 5;        // coast this many frames when target drops
    constexpr uint8_t  EVAC_GRAB_STOP_WINDOW      = 5;        // over-height vote window
    constexpr uint8_t  EVAC_GRAB_STOP_REQUIRED    = 3;        // ...hits needed (3 of 5)
    // Collection / deploy policy:
    constexpr uint32_t EVAC_SEARCH_TIMEOUT_MS     = 120000UL; // 2-min collection window
    constexpr uint32_t SPIN_SEARCH_MS             = 10000UL;  // spin in place this long with no victim, then roam forward
    constexpr float    EVAC_POINT_STOP_WIDTH_PX   = 300.0f;   // corner-approach stop width
    constexpr float    EVAC_POINT_STOP_HEIGHT_PX  = 140.0f;    // corner-approach stop height
    constexpr uint8_t  EVAC_POINT_STOP_REQUIRED   = 5;        // consecutive close frames at the corner
    constexpr int      EVAC_POINT_ALIGN_DEADBAND_PX = 25;     // centre band before colour read
    constexpr int      EVAC_POINT_ALIGN_SPEED_MIN   = 30;
    constexpr int      EVAC_POINT_ALIGN_SPEED_MAX   = 50;
    constexpr int      EVAC_POINT_ALIGN_MOVEMS_MIN  = 20;
    constexpr int      EVAC_POINT_ALIGN_MOVEMS_MAX  = 150;
    constexpr long     EVAC_POINT_ALIGN_MOVEMS_K    = 130;
    constexpr uint32_t EVAC_POINT_ALIGN_TIMEOUT_MS  = 2500;
    constexpr uint32_t EVAC_DEPLOY_TIMEOUT_MS     = 30000UL;  // give up hunting the corner after this
    constexpr uint32_t MODEL_SWAP_MS              = 1500;     // wait for the K230 to load a model
    constexpr int      DEPLOY_TOUCH_SPEED         = 50;       // drive-into-corner speed
    constexpr float    DEPLOY_TOUCH_TURN_GAIN     = 35.0f;    // keep corner centred while touching
    constexpr uint32_t DEPLOY_TOUCH_TIMEOUT_MS    = 4000;     // safety if the bumper never triggers
    constexpr uint32_t DEPLOY_TOUCH_CONFIRM_MS    = 50;       // ignore one-frame bumper noise
    constexpr int      DEPLOY_BACKOFF_SPEED       = -50;      // back off after release / wrong colour
    constexpr int      DEPLOY_BACKOFF_MM          = 100;
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

    void driveTowardDirection(float dir) {
        const float turn  = dir * EVAC_GRAB_TURN_GAIN;
        Actions::Drive::motor(EVAC_GRAB_BASE_SPEED + turn, EVAC_GRAB_BASE_SPEED - turn);
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

        while (true) {
            Processing::K230Decode::tick();
            const K230DBox* t = (onlyCls == K230_CLASS_ALIVE) ? closestCenterLiveVictim()
                                                              : closestCenterVictim();

            if (t == nullptr) {
                if (lost++ >= EVAC_GRAB_LOST_HOLD_FRAMES) {
                    Actions::Drive::stop();
                    return -1;
                }
                driveTowardDirection(smoothedDir);   // coast on last smoothed dir
                delay(20);
                continue;
            }
            lost = 0;

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

            driveTowardDirection(smoothedDir);
            delay(20);
        }
    }

    // After switching to the points model, vote the corner colour over a few
    // frames. Returns K230_POINT_GREEN / K230_POINT_RED, or -1 if undecided.
    int classifyPointColor() {
        int red = 0, green = 0;
        for (uint8_t i = 0; i < 8; i++) {
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
        while (millis() - start < EVAC_DEPLOY_TIMEOUT_MS) {
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
                driveTowardDirection(directionFor(*corner));
            }
        }
        return false;
    }

    bool alignPointToCenter(float* outDir = nullptr) {
        const uint32_t start = millis();
        while (millis() - start < EVAC_POINT_ALIGN_TIMEOUT_MS) {
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
        while (millis() - t0 < DEPLOY_TOUCH_TIMEOUT_MS) {
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
        while (millis() - start < EVAC_DEPLOY_TIMEOUT_MS) {
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
                Actions::Forward::forward(100, 80);
                return true;
            }
            // wrong corner -> back off + spin, look for another one.
            Actions::Forward::forward(-60, 50);
            Actions::Drive::spinDecay(70, 2000);
            Actions::Forward::forward(60, 200);
        }
        return false;
    }

    // Deposit live balls at the GREEN corner, then back off.
    void deployGreen() {
        Serial.println("[deploy] GREEN (live)");
        driveToColorCorner(K230_POINT_GREEN);   
        VictimManager::releaseLive();
        Actions::Forward::forward(DEPLOY_BACKOFF_SPEED, DEPLOY_BACKOFF_MM);
    }

    // Deposit the dead ball at the RED corner, then back off.
    void deployRed() {
        Serial.println("[deploy] RED (dead)");
        driveToColorCorner(K230_POINT_RED);
        VictimManager::releaseDead();
        Actions::Forward::forward(DEPLOY_BACKOFF_SPEED, DEPLOY_BACKOFF_MM);
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_SEARCH_DEPLOY");
#endif
    VictimManager::reset();
}

void update() {
    const uint32_t start = millis();
    uint32_t lastSeen = millis();   // last time a victim was in view (roam timer)

    // Collect-and-deploy loop for the 2-minute window.
    while (millis() - start < EVAC_SEARCH_TIMEOUT_MS) {
        // >= 2 live held -> drop them at the green corner, then keep collecting.
        // (A dead ball, if held, is carried until the timer triggers a red deploy.)
        if (VictimManager::readyToDeploy()) {
            digitalWrite(LED_BUILTIN, HIGH);
            deployGreen();  // calls driveToColorCorner(int targetcolor)
            lastSeen = millis();   // fresh search window after a deploy
            continue;
        }

        Processing::K230Decode::drainDelay(50);   // always decide on a fresh frame

        // closestCenterVictim is already filtered to types we still want.
        if (closestCenterVictim() != nullptr) {
            lastSeen = millis();                 // reset roam timer: something in view
            const int type = approachVictim();   // run and stop at front of victim.
            if (type >= 0) {
                VictimManager::tryGrab((uint8_t)type);   // grab + self-confirm
                Actions::Drive::stop();

                const uint32_t seenPacketMs = Processing::K230Decode::lastPacketMs();
                Processing::K230Decode::waitForFreshFrameAfter(seenPacketMs, 700);

            }
        } else if (millis() - lastSeen < SPIN_SEARCH_MS) {
            // Sweep in place to find a victim.
            Actions::Drive::motor(50, -50);
        } else {
            // Nothing found while spinning for SPIN_SEARCH_MS: roam forward to a new
            // spot, turning away when the front bumper hits, instead of spinning on
            // the same blind corner.
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) Actions::Drive::motor(50, -50);  // blocked -> turn in place
            else                         Actions::Drive::motor(50, 50);   // roam forward
        }
    }

    // Timer expired: drop whatever we still hold (live->green, dead->red), leave.
    if (VictimManager::liveHeld() > 0) deployGreen();
    if (VictimManager::deadHeld() > 0) deployRed();

    Actions::Drive::stop();
    StateMachine::transitionTo(StateMachine::EVAC_EXIT);
}

}  // namespace EVAC_SearchDeploy
