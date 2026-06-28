#include "VictimManager.h"
#include "../../config.h"
#include "../actions/Arm.h"
#include "../actions/Drive.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

// =============================================================================
//  VictimManager — the ONE place all grabbing logic lives.
//    pickArm()        : choose which arm (natural + overflow, capacities)
//    alignToBall()    : drive so the ball sits in front of that arm
//    captureWithArm() : open->back->down->fwd->close->carry choreography
//    confirmCaptured(): did the ball actually leave the floor?
//  Everything it does to the robot is a plain Arm / Drive / K230 primitive call.
// =============================================================================

namespace VictimManager {

namespace {
    // --- arm capacities ---------------------------------------------------------
    constexpr uint8_t LEFT_CAP  = 2;   // LEFT arm holds two balls (LIFO)
    constexpr uint8_t RIGHT_CAP = 1;   // RIGHT arm holds one
    constexpr uint8_t EVAC_MAX_BALLS = LEFT_CAP + RIGHT_CAP;

    enum Side { SIDE_LEFT, SIDE_RIGHT };

    // --- capture tuning (offsets / speeds / timings are per physical arm) --------
    constexpr int     ALIGN_OFFSET_LEFT  = +120;   // ball left-of-centre for LEFT arm
    constexpr int     ALIGN_OFFSET_RIGHT = -120;
    constexpr int     ALIGN_DEADBAND_PX  = 25;
    constexpr int     ALIGN_SPEED_MIN    = 30;
    constexpr int     ALIGN_SPEED_MAX    = 50;
    constexpr int     ALIGN_MOVEMS_MIN   = 20;
    constexpr int     ALIGN_MOVEMS_MAX   = 150;
    constexpr long    ALIGN_MOVEMS_K     = 130;    // quadratic pulse coefficient
    constexpr uint8_t ALIGN_LOST_RETRY   = 4;      // re-poll this many frames on dropout

    constexpr int     GRAB_BACK_SPEED    = -15;
    constexpr int     GRAB_FWD_SPEED     = 35;
    constexpr int     LEFT_DOWN_MS  = 800,  LEFT_FWD_MS  = 1000;
    constexpr int     RIGHT_DOWN_MS = 600,  RIGHT_FWD_MS = 700;
    constexpr int     CARRY_MS      = 800;

    constexpr float   CONFIRM_FRAC  = 0.6f;        // x EVAC_GRAB_STOP_HEIGHT_PX

    // --- held state -------------------------------------------------------------
    uint8_t _leftStack[LEFT_CAP]   = {};   // type per slot, in grab order
    uint8_t _rightStack[RIGHT_CAP] = {};
    uint8_t _leftCount  = 0;
    uint8_t _rightCount = 0;

    bool leftHasSpace()  { return _leftCount  < LEFT_CAP;  }
    bool rightHasSpace() { return _rightCount < RIGHT_CAP; }

    // Natural arm: alive -> LEFT, dead -> RIGHT. Overflow to the other when full.
    bool pickArm(uint8_t type, Side& out) {
        const Side natural = (type == K230_CLASS_ALIVE) ? SIDE_LEFT : SIDE_RIGHT;
        if (natural == SIDE_LEFT) {
            if (leftHasSpace())  { out = SIDE_LEFT;  return true; }
            if (rightHasSpace()) { out = SIDE_RIGHT; return true; }
        } else {
            if (rightHasSpace()) { out = SIDE_RIGHT; return true; }
            if (leftHasSpace())  { out = SIDE_LEFT;  return true; }
        }
        return false;   // both full
    }

    void record(Side side, uint8_t type) {
        if (side == SIDE_LEFT) _leftStack[_leftCount++]   = type;
        else                   _rightStack[_rightCount++] = type;
    }

    // Drive so `cls` sits at frame-centre + offset. Proportional, tolerant of
    // brief dropouts. Returns true if aligned, false if the ball was lost.
    bool alignToBall(uint8_t cls, int offset) {
        Processing::K230Decode::tick();
        int16_t centerX = Processing::K230Decode::largestCenterX(cls);
        if (centerX < 0) return false;
        int16_t delta = centerX - (int16_t)K230_FRAME_CENTER_X + offset;

        while (abs(delta) > ALIGN_DEADBAND_PX) {
            const int absD   = abs(delta);
            const int speed  = constrain(map(absD, 8, 320, ALIGN_SPEED_MIN, ALIGN_SPEED_MAX),
                                         ALIGN_SPEED_MIN, ALIGN_SPEED_MAX);
            const int moveMs = constrain(ALIGN_MOVEMS_MIN + (int)((ALIGN_MOVEMS_K * absD * absD) / 102400L),
                                         ALIGN_MOVEMS_MIN, ALIGN_MOVEMS_MAX);
            const int dir = (delta < 0) ? -1 : 1;

            Actions::Drive::motor(dir * speed, -dir * speed);
            Processing::K230Decode::drainDelay(moveMs);
            Actions::Drive::stop();
            Processing::K230Decode::drainDelay(100);

            centerX = Processing::K230Decode::largestCenterX(cls);
            for (uint8_t r = 0; r < ALIGN_LOST_RETRY && centerX < 0; r++) {
                Processing::K230Decode::drainDelay(100);
                centerX = Processing::K230Decode::largestCenterX(cls);
            }
            if (centerX < 0) return false;
            delta = centerX - (int16_t)K230_FRAME_CENTER_X + offset;
        }
        Actions::Drive::stop();
        return true;
    }

    // Full grab choreography for one arm — built only from Arm / Drive primitives.
    void captureWithArm(Side side, uint8_t cls) {
        const int offset = (side == SIDE_LEFT) ? ALIGN_OFFSET_LEFT : ALIGN_OFFSET_RIGHT;
        if (!alignToBall(cls, offset)) {
            Actions::Drive::stop();
            return;                       // lost during align -> confirm won't count it
        }

        const int tDown = (side == SIDE_LEFT) ? LEFT_DOWN_MS : RIGHT_DOWN_MS;
        const int tFwd  = (side == SIDE_LEFT) ? LEFT_FWD_MS  : RIGHT_FWD_MS;

        if (side == SIDE_LEFT) Actions::Arm::grabLeft(false); else Actions::Arm::grabRight(false);
        Actions::Drive::motor(GRAB_BACK_SPEED, GRAB_BACK_SPEED);   // back off
        Actions::Arm::liftDown();
        Processing::K230Decode::drainDelay(tDown);
        Actions::Drive::motor(GRAB_FWD_SPEED, GRAB_FWD_SPEED);     // lunge in
        Processing::K230Decode::drainDelay(tFwd);
        if (side == SIDE_LEFT) Actions::Arm::grabLeft(true); else Actions::Arm::grabRight(true);
        Actions::Arm::liftCarry();
        Processing::K230Decode::drainDelay(CARRY_MS);
        Actions::Drive::stop();
    }

    // True if no same-type ball remains tall in view -> we really took it.
    bool confirmCaptured(uint8_t type) {
        Processing::K230Decode::tick();
        const int16_t h      = Processing::K230Decode::largestHeight(type);
        const int16_t thresh = (int16_t)(EVAC_GRAB_STOP_HEIGHT_PX * CONFIRM_FRAC);
        return h < thresh;   // h == -1 (none visible) counts as captured
    }
}

void reset()    { _leftCount = 0; _rightCount = 0; }
void clearAll() { _leftCount = 0; _rightCount = 0; }

uint8_t count() { return (uint8_t)(_leftCount + _rightCount); }
bool    full()  { return count() >= EVAC_MAX_BALLS; }

bool tryGrab(uint8_t type) {
    Side side;
    if (!pickArm(type, side)) {
        Serial.println("[VictimManager] no arm space");
        return false;
    }

    captureWithArm(side, type);

    if (!confirmCaptured(type)) {
        Serial.printf("[VictimManager] grab FAILED type=%u (ball still visible)\n", type);
        return false;
    }

    record(side, type);
    Serial.printf("[VictimManager] grabbed type=%u side=%s  total=%u (L=%u R=%u)\n",
                  type, side == SIDE_LEFT ? "LEFT" : "RIGHT",
                  count(), _leftCount, _rightCount);
    return true;
}

}  // namespace VictimManager
