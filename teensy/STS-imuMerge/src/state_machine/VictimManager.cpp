#include "VictimManager.h"
#include "../../config.h"
#include "../actions/Arm.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

namespace VictimManager {

namespace {
    constexpr uint8_t LEFT_CAP  = 2;   // LEFT arm holds two balls (LIFO)
    constexpr uint8_t RIGHT_CAP = 1;   // RIGHT arm holds one

    uint8_t _leftStack[LEFT_CAP]   = {};   // type per slot, in grab order
    uint8_t _rightStack[RIGHT_CAP] = {};
    uint8_t _leftCount  = 0;
    uint8_t _rightCount = 0;

    bool leftHasSpace()  { return _leftCount  < LEFT_CAP;  }
    bool rightHasSpace() { return _rightCount < RIGHT_CAP; }

    // Natural arm: alive/silver -> LEFT, dead/black -> RIGHT. Overflow to the
    // other arm when the natural one is full.
    bool pickArm(uint8_t type, Actions::Arm::Side& out) {
        const bool aliveType = (type == K230_CLASS_ALIVE);
        const Actions::Arm::Side natural = aliveType ? Actions::Arm::LEFT
                                                     : Actions::Arm::RIGHT;
        if (natural == Actions::Arm::LEFT) {
            if (leftHasSpace())  { out = Actions::Arm::LEFT;  return true; }
            if (rightHasSpace()) { out = Actions::Arm::RIGHT; return true; }
        } else {
            if (rightHasSpace()) { out = Actions::Arm::RIGHT; return true; }
            if (leftHasSpace())  { out = Actions::Arm::LEFT;  return true; }
        }
        return false;   // both arms full
    }

    void record(Actions::Arm::Side side, uint8_t type) {
        if (side == Actions::Arm::LEFT) _leftStack[_leftCount++]   = type;
        else                            _rightStack[_rightCount++] = type;
    }

    // True if no same-type ball remains tall in view — i.e. we really took it.
    bool confirmCaptured(uint8_t type) {
        Processing::K230Decode::tick();
        const int16_t h      = Processing::K230Decode::largestHeight(type);
        const int16_t thresh = (int16_t)(EVAC_GRAB_STOP_HEIGHT_PX * 0.6f);
        return h < thresh;   // h == -1 (none visible) counts as captured
    }
}

void reset()    { _leftCount = 0; _rightCount = 0; }
void clearAll() { _leftCount = 0; _rightCount = 0; }

uint8_t count() { return (uint8_t)(_leftCount + _rightCount); }
bool    full()  { return count() >= EVAC_MAX_BALLS; }

bool tryGrab(uint8_t type) {
    Actions::Arm::Side side;
    if (!pickArm(type, side)) {
        Serial.println("[VictimManager] no arm space");
        return false;
    }

    Actions::Arm::grabArm(side, type);

    if (!confirmCaptured(type)) {
        Serial.printf("[VictimManager] grab FAILED type=%u (ball still visible)\n", type);
        return false;
    }

    record(side, type);
    Serial.printf("[VictimManager] grabbed type=%u side=%s  total=%u (L=%u R=%u)\n",
                  type, side == Actions::Arm::LEFT ? "LEFT" : "RIGHT",
                  count(), _leftCount, _rightCount);
    return true;
}

}  // namespace VictimManager
