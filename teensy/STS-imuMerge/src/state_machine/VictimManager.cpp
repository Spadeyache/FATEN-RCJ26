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
    constexpr uint8_t LEFT_CAP  = 2;   // LEFT arm holds two balls (LIFO via bucket)
    constexpr uint8_t RIGHT_CAP = 1;   // RIGHT arm holds one
    constexpr uint8_t EVAC_MAX_BALLS = LEFT_CAP + RIGHT_CAP;
    constexpr uint8_t DEPLOY_MIN_LIVE = 2;   // deploy once we hold this many live balls

    enum Side { SIDE_LEFT, SIDE_RIGHT };

    // --- capture tuning (offsets / speeds / timings are per physical arm) --------
    constexpr int     ALIGN_OFFSET_LEFT  = +90;   // ball left-of-centre for LEFT arm
    constexpr int     ALIGN_OFFSET_RIGHT = -90;
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
    constexpr int     STORE_MS      = 800;
    constexpr int     RELEASE_MS    = 625;

    constexpr uint8_t CONFIRM_SAMPLE_FRAMES = 3;
    constexpr uint8_t CONFIRM_CLEAR_REQUIRED = 2;
    constexpr uint32_t CONFIRM_FRAME_TIMEOUT_MS = 500;
    constexpr float CAPTURE_ABORT_HEIGHT_PX = EVAC_GRAB_STOP_HEIGHT_PX - 10.0f;

    // --- held state -------------------------------------------------------------
    uint8_t _leftStack[LEFT_CAP]   = {};   // type per slot, in grab order
    uint8_t _rightStack[RIGHT_CAP] = {};
    uint8_t _leftCount  = 0;
    uint8_t _rightCount = 0;

    bool leftHasSpace()  { return _leftCount  < LEFT_CAP;  }
    bool rightHasSpace() { return _rightCount < RIGHT_CAP; }

    // ========================================================================
    //  Arm selection + held-stack bookkeeping
    // ========================================================================

    // DEAD -> RIGHT only (at most one dead). ALIVE -> LEFT first, then RIGHT.
    bool pickArm(uint8_t type, Side& out) {
        if (type == K230_CLASS_DEAD) {
            if (rightHasSpace()) { out = SIDE_RIGHT; return true; }
            return false;
        }
        if (leftHasSpace())  { out = SIDE_LEFT;  return true; }
        if (rightHasSpace()) { out = SIDE_RIGHT; return true; }
        return false;
    }

    void record(Side side, uint8_t type) {
        if (side == SIDE_LEFT) _leftStack[_leftCount++]   = type;
        else                   _rightStack[_rightCount++] = type;
    }

    uint8_t countType(uint8_t type) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < _leftCount;  i++) if (_leftStack[i]  == type) n++;
        for (uint8_t i = 0; i < _rightCount; i++) if (_rightStack[i] == type) n++;
        return n;
    }

    // ========================================================================
    //  Capture motion (vision align + blocking grab choreography)
    // ========================================================================

    void storeFirstLeftLive() {
        Actions::Arm::store();
        Processing::K230Decode::drainDelay(STORE_MS);
        Actions::Arm::releaseLeft();
        Processing::K230Decode::drainDelay(STORE_MS);
        Actions::Arm::liftCarry();
        Processing::K230Decode::drainDelay(CARRY_MS);
        Actions::Arm::grabLeft(true);
    }

    bool closeEnoughForCapture(uint8_t cls) {
        const int16_t h = Processing::K230Decode::largestHeight(cls);
        return h >= CAPTURE_ABORT_HEIGHT_PX;
    }

    // Drive so `cls` sits at frame-centre + offset. Proportional, tolerant of
    // brief dropouts. Returns true if aligned, false if the ball was lost.
    bool alignToBall(uint8_t cls, int offset) {
        Processing::K230Decode::tick();
        int16_t centerX = Processing::K230Decode::largestCenterX(cls);
        if (centerX < 0) return false;
        if (!closeEnoughForCapture(cls)) return false;
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
            if (!closeEnoughForCapture(cls)) return false;
            delta = centerX - (int16_t)K230_FRAME_CENTER_X + offset;
        }
        Actions::Drive::stop();
        return true;
    }

    // Full grab choreography for one arm — built only from Arm / Drive primitives.
    bool captureWithArm(Side side, uint8_t cls) {
        const int offset = (side == SIDE_LEFT) ? ALIGN_OFFSET_LEFT : ALIGN_OFFSET_RIGHT;
        if (!alignToBall(cls, offset)) {
            Actions::Drive::stop();
            return false;
        }
        if (!closeEnoughForCapture(cls)) return false;

        // const int tDown = (side == SIDE_LEFT) ? LEFT_DOWN_MS : RIGHT_DOWN_MS;
        // const int tFwd  = (side == SIDE_LEFT) ? LEFT_FWD_MS  : RIGHT_FWD_MS;

        if (side == SIDE_LEFT) Actions::Arm::grabLeft(false); else Actions::Arm::grabRight(false);
        Actions::Drive::motor(-18, -18);   // back off
        Actions::Arm::liftDown();
        Processing::K230Decode::drainDelay(570);
        Actions::Drive::motor(50, 50);     // lunge in
        Processing::K230Decode::drainDelay(615);
        if (side == SIDE_LEFT) Actions::Arm::grabLeft(true); else Actions::Arm::grabRight(true);
        Actions::Drive::motor(-50, -50);   // back off
        Processing::K230Decode::drainDelay(300);

        Actions::Arm::liftCarry();
        Processing::K230Decode::drainDelay(CARRY_MS - 600);
        Actions::Drive::stop();
        Processing::K230Decode::drainDelay(600);

        // First ball on the left goes into the bucket so the gripper is free for a
        // second one (LIFO). The second one stays in the gripper.
        Actions::Drive::stop();
        if (side == SIDE_LEFT && _leftCount == 0) {
            storeFirstLeftLive();
        }
        return true;
    }

    // True if fresh post-grab frames no longer show this type.
    bool confirmCaptured(uint8_t type) {
        uint8_t clearFrames = 0;
        uint32_t seenPacketMs = Processing::K230Decode::lastPacketMs();

        for (uint8_t i = 0; i < CONFIRM_SAMPLE_FRAMES; i++) {
            if (!Processing::K230Decode::waitForFreshFrameAfter(seenPacketMs, CONFIRM_FRAME_TIMEOUT_MS)) {
                Serial.println("[VictimManager] confirm timeout waiting fresh K230 frame");
                return false;
            }
            seenPacketMs = Processing::K230Decode::lastPacketMs();

            const int16_t h = Processing::K230Decode::largestHeight(type);
            if (h < 0) clearFrames++;
        }

        return clearFrames >= CONFIRM_CLEAR_REQUIRED;
    }
}

// ============================================================================
//  Public API
// ============================================================================

// Lifecycle: reset() at evac start, clearAll() after a deploy empties the arms.
void reset()    { _leftCount = 0; _rightCount = 0; }
void clearAll() { _leftCount = 0; _rightCount = 0; }

// Held-count queries used by the search/deploy loop.
uint8_t count()    { return (uint8_t)(_leftCount + _rightCount); }
uint8_t liveHeld() { return countType(K230_CLASS_ALIVE); }
uint8_t deadHeld() { return countType(K230_CLASS_DEAD); }
bool    full()     { return count() >= EVAC_MAX_BALLS; }

// DEAD: only if we hold none yet and the right arm is free. ALIVE: any space.
bool acceptsType(uint8_t type) {
    if (type == K230_CLASS_DEAD)  return deadHeld() == 0 && rightHasSpace();
    if (type == K230_CLASS_ALIVE) return leftHasSpace() || rightHasSpace();
    return false;
}

bool readyToDeploy() { return liveHeld() >= DEPLOY_MIN_LIVE; }

// Grab one ball of `type`: choose an arm -> run the capture -> self-confirm ->
// only book it on success. Returns true iff a ball was actually captured.
bool tryGrab(uint8_t type) {
    Side side;
    if (!pickArm(type, side)) { //pickArm writes the chosen arm INTO side,and returns false if both arms are full
        Serial.println("[VictimManager] no arm space");
        return false;
    }

    if (!captureWithArm(side, type)) {
        Serial.printf("[VictimManager] grab ABORT type=%u (victim no longer close)\n", type);
        return false;
    }

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

// Left arm only ever holds live; the right may hold a live overflow.
void releaseLive() {
    Actions::Arm::liftRelease();
    Processing::K230Decode::drainDelay(RELEASE_MS);
    Actions::Arm::releaseLeft();
    if (_rightCount > 0 && _rightStack[0] == K230_CLASS_ALIVE) {
        Actions::Arm::releaseRight();
        _rightCount = 0;
    }
    Processing::K230Decode::drainDelay(20);

    Actions::Arm::grabLeft(false);
    Actions::Arm::liftPark();  // release the stored victim
    Processing::K230Decode::drainDelay(RELEASE_MS);
    Processing::K230Decode::drainDelay(120);
    Actions::Arm::liftCarry();
    Processing::K230Decode::drainDelay(300);
    Actions::Arm::liftPark();  // confimr extar
    Processing::K230Decode::drainDelay(420); // time to settle
    Actions::Arm::grabLeft(true);
    Processing::K230Decode::drainDelay(135); // time to settle
    Actions::Arm::liftRelease();
    Processing::K230Decode::drainDelay(RELEASE_MS+150); //end of release stored victim
    Actions::Arm::releaseLeft();

    Actions::Arm::liftCarry(); // go back to evac start krs posision.
    Processing::K230Decode::drainDelay(CARRY_MS);

    _leftCount = 0;
}

// The dead ball only ever sits in the right arm.
void releaseDead() {
    Actions::Arm::liftRelease();
    Processing::K230Decode::drainDelay(300);

    if (_rightCount > 0 && _rightStack[0] == K230_CLASS_DEAD) {
        Actions::Arm::releaseRight();
        _rightCount = 0;
    }
}

}  // namespace VictimManager
