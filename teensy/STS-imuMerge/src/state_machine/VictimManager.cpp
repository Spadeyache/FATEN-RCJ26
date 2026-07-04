#include "VictimManager.h"
#include "../../config.h"
#include "../actions/Arm.h"
#include "../actions/Drive.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

namespace VictimManager {

namespace {
    enum Side { SIDE_LEFT, SIDE_RIGHT };

    constexpr int     ALIGN_OFFSET_LEFT  = +90;
    constexpr int     ALIGN_OFFSET_RIGHT = -90;
    constexpr int     ALIGN_DEADBAND_PX  = 25;
    constexpr int     ALIGN_SPEED_MIN    = 30;
    constexpr int     ALIGN_SPEED_MAX    = 50;
    constexpr int     ALIGN_MOVEMS_MIN   = 20;
    constexpr int     ALIGN_MOVEMS_MAX   = 150;
    constexpr long    ALIGN_MOVEMS_K     = 130;
    constexpr uint8_t ALIGN_LOST_RETRY   = 4;

    constexpr int     CARRY_MS      = 800;
    constexpr int     RELEASE_MS    = 625;
    constexpr uint8_t CONFIRM_SAMPLE_FRAMES = 3;
    constexpr uint8_t CONFIRM_CLEAR_REQUIRED = 2;
    constexpr uint32_t CONFIRM_FRAME_TIMEOUT_MS = 500;
    constexpr float CAPTURE_ABORT_HEIGHT_PX = EVAC_GRAB_STOP_HEIGHT_PX - 10.0f;

    bool    _leftFull = false;
    bool    _rightFull = false;
    uint8_t _leftType = 0;
    uint8_t _rightType = 0;

    bool leftHasSpace() { return !_leftFull; }
    bool rightHasSpace() { return !_rightFull; }

    bool pickArm(Side& out) {
        if (leftHasSpace()) {
            out = SIDE_LEFT;
            return true;
        }
        if (rightHasSpace()) {
            out = SIDE_RIGHT;
            return true;
        }
        return false;
    }

    void record(Side side, uint8_t type) {
        if (side == SIDE_LEFT) {
            _leftFull = true;
            _leftType = type;
        } else {
            _rightFull = true;
            _rightType = type;
        }
    }

    uint8_t countType(uint8_t type) {
        uint8_t n = 0;
        if (_leftFull && _leftType == type) n++;
        if (_rightFull && _rightType == type) n++;
        return n;
    }

    bool closeEnoughForCapture(uint8_t cls) {
        const int16_t h = Processing::K230Decode::largestHeight(cls);
        return h >= CAPTURE_ABORT_HEIGHT_PX;
    }

    bool alignToBall(uint8_t cls, int offset) {
        Processing::K230Decode::tick();
        int16_t centerX = Processing::K230Decode::largestCenterX(cls);
        if (centerX < 0) return false;
        if (!closeEnoughForCapture(cls)) return false;
        int16_t delta = centerX - (int16_t)K230_FRAME_CENTER_X + offset;

        while (abs(delta) > ALIGN_DEADBAND_PX) {
            const int absD = abs(delta);
            const int speed = constrain(map(absD, 8, 320, ALIGN_SPEED_MIN, ALIGN_SPEED_MAX),
                                        ALIGN_SPEED_MIN, ALIGN_SPEED_MAX);
            const int moveMs = constrain(ALIGN_MOVEMS_MIN +
                                         (int)((ALIGN_MOVEMS_K * absD * absD) / 102400L),
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

    bool captureWithArm(Side side, uint8_t cls) {
        const int offset = (side == SIDE_LEFT) ? ALIGN_OFFSET_LEFT : ALIGN_OFFSET_RIGHT;
        if (!alignToBall(cls, offset)) {
            Actions::Drive::stop();
            return false;
        }
        if (!closeEnoughForCapture(cls)) return false;

        if (side == SIDE_LEFT) Actions::Arm::grabLeft(false);
        else                   Actions::Arm::grabRight(false);

        Actions::Drive::motor(-18, -18);
        Actions::Arm::liftDown();
        Processing::K230Decode::drainDelay(570);
        Actions::Drive::motor(50, 50);
        Processing::K230Decode::drainDelay(615);

        if (side == SIDE_LEFT) Actions::Arm::grabLeft(true);
        else                   Actions::Arm::grabRight(true);

        Actions::Drive::motor(-50, -50);
        Processing::K230Decode::drainDelay(300);
        Actions::Arm::liftCarry();
        Processing::K230Decode::drainDelay(CARRY_MS);
        Actions::Drive::stop();
        return true;
    }

    bool confirmCaptured(uint8_t type) {
        uint8_t clearFrames = 0;
        uint32_t seenPacketMs = Processing::K230Decode::lastPacketMs();

        for (uint8_t i = 0; i < CONFIRM_SAMPLE_FRAMES; i++) {
            if (!Processing::K230Decode::waitForFreshFrameAfter(seenPacketMs, CONFIRM_FRAME_TIMEOUT_MS)) {
                Serial.println("[VictimManager] confirm timeout waiting fresh K230 frame");
                return false;
            }
            seenPacketMs = Processing::K230Decode::lastPacketMs();

            if (Processing::K230Decode::largestHeight(type) < 0) clearFrames++;
        }

        return clearFrames >= CONFIRM_CLEAR_REQUIRED;
    }
}

void reset() {
    _leftFull = false;
    _rightFull = false;
}

void clearAll() { reset(); }

uint8_t count() {
    return (uint8_t)((_leftFull ? 1 : 0) + (_rightFull ? 1 : 0));
}

uint8_t liveHeld() { return countType(K230_CLASS_ALIVE); }
uint8_t deadHeld() { return countType(K230_CLASS_DEAD); }
bool full() { return _leftFull && _rightFull; }

bool acceptsType(uint8_t type) {
    return Processing::K230Decode::isVictimClass(type) && !full();
}

bool readyToDeploy() { return full(); }

bool tryGrab(uint8_t type) {
    Side side;
    if (!pickArm(side)) {
        Serial.println("[VictimManager] no arm space");
        return false;
    }

    if (!captureWithArm(side, type)) {
        Serial.printf("[VictimManager] grab ABORT type=%u\n", type);
        return false;
    }

    if (!confirmCaptured(type)) {
        Serial.printf("[VictimManager] grab FAILED type=%u\n", type);
        return false;
    }

    record(side, type);
    Serial.printf("[VictimManager] grabbed type=%u side=%s total=%u\n",
                  type, side == SIDE_LEFT ? "LEFT" : "RIGHT", count());
    return true;
}

void releaseLive() {
    Actions::Arm::liftRelease();
    Processing::K230Decode::drainDelay(RELEASE_MS);
    if (_leftFull && _leftType == K230_CLASS_ALIVE) {
        Actions::Arm::releaseLeft();
        _leftFull = false;
    }
    if (_rightFull && _rightType == K230_CLASS_ALIVE) {
        Actions::Arm::releaseRight();
        _rightFull = false;
    }
    Processing::K230Decode::drainDelay(RELEASE_MS);
    Actions::Arm::liftCarry();
}

void releaseDead() {
    Actions::Arm::liftRelease();
    Processing::K230Decode::drainDelay(RELEASE_MS);
    if (_leftFull && _leftType == K230_CLASS_DEAD) {
        Actions::Arm::releaseLeft();
        _leftFull = false;
    }
    if (_rightFull && _rightType == K230_CLASS_DEAD) {
        Actions::Arm::releaseRight();
        _rightFull = false;
    }
    Processing::K230Decode::drainDelay(RELEASE_MS);
    Actions::Arm::liftCarry();
}

}  // namespace VictimManager
