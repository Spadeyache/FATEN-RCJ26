#include "EVAC.h"
#include "StateMachine.h"
#include "VictimManager.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../actions/Drive.h"
#include "../actions/Forward.h"
#include "../actions/Arm.h"
#include "../processing/K230Decode.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>

namespace EVAC {

namespace {
    constexpr float    CHASE_SPEED_FAR            = 65.0f;
    constexpr float    CHASE_SPEED_NEAR           = 45.0f;
    constexpr float    CHASE_TURN_GAIN            = 60.0f;
    constexpr uint8_t  LOST_HOLD_FRAMES           = 5;
    constexpr uint8_t  STOP_WINDOW                = 5;
    constexpr uint8_t  STOP_REQUIRED              = 3;
    constexpr uint32_t VICTIM_SPIN_MS             = 8000UL;
    constexpr uint32_t SEARCH_FORWARD_MS          = 5000UL;
    constexpr int      SEARCH_FORWARD_SPEED       = 60;
    constexpr float    EXIT_STOP_HEIGHT_PX        = 150.0f;
    constexpr uint8_t  EXIT_STOP_REQUIRED         = 4;

    float absF(float v) { return v < 0.0f ? -v : v; }

    float boxHeightPx(const K230DBox& b) {
        return absF((float)b.y2 - (float)b.y1);
    }

    float directionFor(const K230DBox& b) {
        const float cx = ((float)b.x1 + (float)b.x2) * 0.5f;
        float dir = (cx - K230_FRAME_CENTER_X) / K230_FRAME_CENTER_X;
        if (dir < -1.0f) dir = -1.0f;
        if (dir >  1.0f) dir =  1.0f;
        return dir;
    }

    float chaseSpeed(float heightPx, float stopHeightPx) {
        float t = heightPx / stopHeightPx;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return CHASE_SPEED_FAR + (CHASE_SPEED_NEAR - CHASE_SPEED_FAR) * t;
    }

    void driveToward(float dir, float baseSpeed) {
        const float turn = dir * CHASE_TURN_GAIN;
        Actions::Drive::motor(baseSpeed + turn, baseSpeed - turn);
    }

    const K230DBox* closestCenterVictim() {
        const K230DBox* boxes = Processing::K230Decode::boxes();
        const uint8_t n = Processing::K230Decode::boxCount();
        const K230DBox* best = nullptr;
        float bestAbs = 999.0f;
        for (uint8_t i = 0; i < n; i++) {
            if (!Processing::K230Decode::isVictimClass(boxes[i].cls)) continue;
            if (!VictimManager::acceptsType(boxes[i].cls)) continue;
            const float a = absF(directionFor(boxes[i]));
            if (best == nullptr || a < bestAbs) {
                best = &boxes[i];
                bestAbs = a;
            }
        }
        return best;
    }

    const K230DBox* closestCenterExit() {
        const K230DBox* boxes = Processing::K230Decode::boxes();
        const uint8_t n = Processing::K230Decode::boxCount();
        const K230DBox* best = nullptr;
        float bestAbs = 999.0f;
        for (uint8_t i = 0; i < n; i++) {
            if (!Processing::K230Decode::isPointClass(boxes[i].cls)) continue;
            const float a = absF(directionFor(boxes[i]));
            if (best == nullptr || a < bestAbs) {
                best = &boxes[i];
                bestAbs = a;
            }
        }
        return best;
    }

    int approachVictim() {
        uint8_t lost = 0;
        bool stopWin[STOP_WINDOW] = {};
        uint8_t stopIdx = 0;
        uint8_t stopCnt = 0;
        float lastDir = 0.0f;

        while (true) {
            Processing::K230Decode::drainDelay(20);
            const K230DBox* target = closestCenterVictim();
            if (target == nullptr) {
                if (lost++ >= LOST_HOLD_FRAMES) {
                    Actions::Drive::stop();
                    return -1;
                }
                driveToward(lastDir, CHASE_SPEED_FAR);
                continue;
            }
            lost = 0;
            lastDir = directionFor(*target);

            stopWin[stopIdx] = boxHeightPx(*target) >= EVAC_GRAB_STOP_HEIGHT_PX;
            stopIdx = (stopIdx + 1) % STOP_WINDOW;
            if (stopCnt < STOP_WINDOW) stopCnt++;
            uint8_t hits = 0;
            for (uint8_t i = 0; i < stopCnt; i++) if (stopWin[i]) hits++;
            if (hits >= STOP_REQUIRED) {
                Actions::Drive::stop();
                return (int)target->cls;
            }

            driveToward(lastDir, chaseSpeed(boxHeightPx(*target), EVAC_GRAB_STOP_HEIGHT_PX));
        }
    }

    bool driveToK230Exit() {
        uint8_t exitHits = 0;
        while (true) {
            Processing::K230Decode::drainDelay(50);
            const K230DBox* exitBox = closestCenterExit();
            if (exitBox == nullptr) {
                Actions::Drive::motor(50, -50);
                continue;
            }

            const float dir = directionFor(*exitBox);
            if (boxHeightPx(*exitBox) >= EXIT_STOP_HEIGHT_PX) {
                if (++exitHits >= EXIT_STOP_REQUIRED) {
                    Actions::Drive::stop();
                    Actions::Forward::forward(60, 120, /*useIMU=*/false, /*pumpComms=*/true);
                    return true;
                }
            } else {
                exitHits = 0;
            }
            driveToward(dir, chaseSpeed(boxHeightPx(*exitBox), EXIT_STOP_HEIGHT_PX));
        }
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC");
#endif
    Actions::Drive::stop();
    Actions::Arm::attachServos();
    Actions::Arm::liftCarry();
    Actions::Arm::grabLeft(true);
    Actions::Arm::grabRight(true);
    VictimManager::reset();
    Processing::K230Decode::setModel(Processing::K230Decode::MODEL_VICTIMS);
    Processing::K230Decode::setRunning(true);
}

void update() {
    uint32_t noVictimPhaseStart = 0;
    bool forwardSearch = false;

    while (!VictimManager::full()) {
        Processing::K230Decode::drainDelay(50);
        if (closestCenterVictim() != nullptr) {
            noVictimPhaseStart = 0;
            forwardSearch = false;
            const int type = approachVictim();
            if (type >= 0) {
                const bool grabbed = VictimManager::tryGrab((uint8_t)type);
                Actions::Drive::stop();
                Actions::Forward::forward(-50, 70, /*useIMU=*/false, /*pumpComms=*/true);
                if (grabbed) {
                    const uint32_t seenPacketMs = Processing::K230Decode::lastPacketMs();
                    Processing::K230Decode::waitForFreshFrameAfter(seenPacketMs, 700);
                }
            }
            continue;
        }

        if (noVictimPhaseStart == 0) {
            noVictimPhaseStart = millis();
            forwardSearch = false;
        }

        const uint32_t phaseMs = millis() - noVictimPhaseStart;
        if (!forwardSearch && phaseMs >= VICTIM_SPIN_MS) {
            forwardSearch = true;
            noVictimPhaseStart = millis();
        } else if (forwardSearch && phaseMs >= SEARCH_FORWARD_MS) {
            forwardSearch = false;
            noVictimPhaseStart = millis();
        }

        if (forwardSearch) {
            Actions::Drive::motor(SEARCH_FORWARD_SPEED, SEARCH_FORWARD_SPEED);
        } else {
            Actions::Drive::motor(50, -50);
        }
    }

    Actions::Drive::stop();
    Processing::K230Decode::setModel(Processing::K230Decode::MODEL_POINTS);
    Processing::K230Decode::drainDelay(1500);
    driveToK230Exit();
    Processing::K230Decode::setModel(Processing::K230Decode::MODEL_VICTIMS);
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
    StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
}

}  // namespace EVAC
