#include "EVAC_SearchDeploy.h"
#include "StateMachine.h"
#include "VictimManager.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../actions/Drive.h"
#include "../actions/Arm.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

namespace EVAC_SearchDeploy {

namespace {
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

    // Nearest-to-centre box of a victim class (DEAD or ALIVE).
    const K230DBox* closestCenterVictim() {
        const K230DBox* boxes = Processing::K230Decode::boxes();
        const uint8_t   n     = Processing::K230Decode::boxCount();
        const K230DBox* best  = nullptr;
        float bestAbs = 999.0f;
        for (uint8_t i = 0; i < n; i++) {
            if (!Processing::K230Decode::isVictimClass(boxes[i].cls)) continue;
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
    int approachVictim() {
        uint8_t lost      = 0;
        uint8_t stopHits  = 0;   // consecutive close-enough frames
        float   lastDir   = 0.0f;

        while (true) {
            Processing::K230Decode::tick();
            const K230DBox* t = closestCenterVictim();

            if (t == nullptr) {
                if (lost++ >= EVAC_GRAB_LOST_HOLD_FRAMES) {
                    Actions::Drive::stop();
                    return -1;
                }
                driveTowardDirection(lastDir);   // coast on last direction
                delay(20);
                continue;
            }
            lost = 0;

            if (boxHeightPx(*t) >= EVAC_GRAB_STOP_HEIGHT_PX) {
                if (++stopHits >= EVAC_GRAB_STOP_REQUIRED) {
                    Actions::Drive::stop();
                    return (int)t->cls;          // reached
                }
            } else {
                stopHits = 0;
            }

            lastDir = directionFor(*t);
            driveTowardDirection(lastDir);
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

    // --- deploy: drive to a corner, classify its colour, release. Blocking. ---
    void runDeploy() {
        Serial.println("[deploy] start - approach corner");

        // Phase A: approach the corner using the victims model's POINT class.
        const uint32_t start = millis();
        uint8_t stopHits = 0;
        bool reached = false;
        while (millis() - start < EVAC_DEPLOY_TIMEOUT_MS) {
            Processing::K230Decode::drainDelay(50);
            const K230DBox* corner = closestCenterPoint();
            if (corner == nullptr) {
                stopHits = 0;
                Actions::Drive::spinDecay(60, 400);
                continue;
            }
            if (boxHeightPx(*corner) >= EVAC_POINT_STOP_HEIGHT_PX) {
                if (++stopHits >= EVAC_POINT_STOP_REQUIRED) {   // 5 consecutive close frames
                    Actions::Drive::stop();
                    reached = true;
                    break;
                }
                Actions::Drive::stop();
            } else {
                stopHits = 0;
                driveTowardDirection(directionFor(*corner));
            }
        }

        if (!reached) {
            Serial.println("[deploy] corner not reached - releasing anyway");
            Actions::Drive::stop();
            Actions::Arm::releaseAll();
            return;
        }

        // Phase B: swap to the points model and classify the corner colour.
        Serial.println("[deploy] reached - loading points model");
        Processing::K230Decode::setModel(Processing::K230Decode::MODEL_POINTS);
        Processing::K230Decode::drainDelay(1500);   // let the K230 load the model

        const int color = classifyPointColor();
        Serial.printf("[deploy] corner colour = %s\n",
                      color == K230_POINT_GREEN ? "GREEN(live)" :
                      color == K230_POINT_RED   ? "RED(dead)"   : "UNKNOWN");

        // Phase C: switch back to the victims model for the next batch.
        Processing::K230Decode::setModel(Processing::K230Decode::MODEL_VICTIMS);
        Processing::K230Decode::drainDelay(1500);

        // Phase D: release. Phase-1 drops everything; colour-routed + LIFO-ordered
        // release (GREEN->alive, RED->dead) is the next step — `color` is known now.
        Actions::Drive::stop();
        Actions::Arm::releaseAll();
        Serial.println("[deploy] released");
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

    // Collect-and-deploy loop for the 2-minute window.
    while (millis() - start < EVAC_SEARCH_TIMEOUT_MS) {
        // Full -> deploy this batch, then keep collecting.
        if (VictimManager::full()) {
            runDeploy();
            VictimManager::clearAll();
            continue;
        }

        Processing::K230Decode::drainDelay(50);   // always decide on a fresh frame

        if (Processing::K230Decode::checkVictim()) {
            const int type = approachVictim();
            if (type >= 0) {
                VictimManager::tryGrab((uint8_t)type);   // grab + self-confirm
            }
        } else {
            Actions::Drive::spinDecay(60, 400);          // sweep for a ball
        }
    }

    // Timer expired: deploy whatever is still held, then leave.
    if (VictimManager::count() > 0) {
        runDeploy();
        VictimManager::clearAll();
    }

    Actions::Drive::stop();
    StateMachine::transitionTo(StateMachine::EVAC_EXIT);
}

}  // namespace EVAC_SearchDeploy
