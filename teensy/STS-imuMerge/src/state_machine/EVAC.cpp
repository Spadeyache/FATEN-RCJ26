#include "EVAC.h"
#include "StateMachine.h"
#include "VictimManager.h"
#include "DeployPlan.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../actions/Drive.h"
#include "../actions/Forward.h"
#include "../actions/Arm.h"
#include "../actions/Turn.h"
#include "../actions/WallFollow.h"
#include "../processing/K230Decode.h"
#include "../processing/XiaoDecode.h"
#include "../sensors/Touch.h"
#include "../sensors/ToF.h"
#include "../sensors/XIAO_link.h"

#include <Arduino.h>

namespace EVAC {

namespace {
    // Chase tuning. Speeds restored to 65/45 2026-07-04 after the K230 fps fix
    // (vectorized decode) removed the jitter root cause. DIR_EMA_ALPHA is the
    // low-pass on steering direction -- the K230 box stream is raw
    // (CommandFilter only smooths XIAO events), so this EMA still damps any
    // residual zigzag. Lower alpha = smoother but lazier.
    constexpr float    CHASE_SPEED_FAR            = 65.0f;
    constexpr float    CHASE_SPEED_NEAR           = 45.0f;
    constexpr float    CHASE_TURN_GAIN            = 60.0f;
    constexpr float    DIR_EMA_ALPHA              = 0.35f;
    constexpr uint8_t  LOST_HOLD_FRAMES           = 5;
    constexpr uint8_t  STOP_WINDOW                = 5;
    constexpr uint8_t  STOP_REQUIRED              = 3;
    constexpr uint32_t VICTIM_SPIN_MS             = 8000UL;
    constexpr uint32_t SEARCH_FORWARD_MS          = 5000UL;
    constexpr int      SEARCH_FORWARD_SPEED       = 60;

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

    // Which colordet colours count as grab targets. Default: every colour the
    // model can emit (0..6). To stop the robot chasing a colour -- e.g. the
    // black line (K230_COLOR_BLACK) or the field markers -- return false for it
    // here. This is the single knob for "which colours do we chase".
    bool isTargetColor(uint8_t cls) {
        return cls < K230_COLOR_COUNT;   // all 7 colours chaseable
    }

    // Closest-to-centre box of ANY chaseable colour we still have a gripper for.
    const K230DBox* closestCenterVictim() {
        const K230DBox* boxes = Processing::K230Decode::boxes();
        const uint8_t n = Processing::K230Decode::boxCount();
        const K230DBox* best = nullptr;
        float bestAbs = 999.0f;
        for (uint8_t i = 0; i < n; i++) {
            if (!isTargetColor(boxes[i].cls)) continue;
            if (!VictimManager::acceptsType(boxes[i].cls)) continue;
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
        bool  dirSeeded = false;

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
            // EMA-smooth the steering direction; raw per-frame boxes jitter.
            // Height votes below stay RAW -- the STOP_WINDOW majority already
            // filters them, and adding lag there would overrun the stop point.
            const float rawDir = directionFor(*target);
            lastDir = dirSeeded ? (DIR_EMA_ALPHA * rawDir + (1.0f - DIR_EMA_ALPHA) * lastDir)
                                : rawDir;
            dirSeeded = true;

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

    // =========================================================================
    //  Exit sequence (ported + simplified from the old EVAC_Exit)
    //
    //  Wall-follow the evac-zone wall until the SILVER exit tape is seen, then
    //  push through it and hand back to LINE_FOLLOW. Black tape is ignored (no
    //  black finish, no silver back-and-turn recovery). WallFollow handles the
    //  front bumper and any wall loss (blind-straight) internally.
    // =========================================================================
    constexpr float EXIT_WALL_TARGET_MM  = 100.0f;
    constexpr float EXIT_WALL_BASE_SPEED = 65.0f;
    constexpr float EXIT_WALL_FAR_MM     = 200.0f;

    constexpr float EXIT_FORWARD_SPEED    = 100.0f;
    constexpr float EXIT_FORWARD_STEP_MM  = 40.0f;
    constexpr float EXIT_BACKUP_SPEED     = 70.0f;
    constexpr float EXIT_BACKUP_MM        = 50.0f;
    constexpr float EXIT_TURN_ANGLE       = -90.0f;   // put the wall on the right
    constexpr float EXIT_TURN_SPEED       = 65.0f;

    constexpr float EXIT_PUSH_SPEED       = 50.0f;    // drive through the silver tape
    constexpr float EXIT_PUSH_MM          = 70.0f;

    // Keep the XIAO in evac color-mask mode (re-send every 100 ms so a dropped
    // command self-heals), tick the link + decode, and report the silver flag.
    bool silverTapeSeen() {
        static uint32_t lastModeSend = 0;
        if (millis() - lastModeSend >= 100) {
            Processing::XiaoDecode::setMode(XIAO_MODE_EVAC_COLOR_MASK);
            lastModeSend = millis();
        }
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
        return Processing::XiaoDecode::silverSeen();
    }

    void driveForwardUntilTouch() {
        Sensors::Touch::tick();
        while (!Sensors::Touch::front()) {
            Actions::Forward::forward(EXIT_FORWARD_SPEED, EXIT_FORWARD_STEP_MM,
                                      /*useIMU=*/false, /*pumpComms=*/true);
            Sensors::Touch::tick();
        }
    }

    // Blocking: runs its own sensor ticks and only returns after queuing the
    // LINE_FOLLOW transition.
    void runExitSequence() {
        Processing::XiaoDecode::setMode(XIAO_MODE_EVAC_COLOR_MASK);

        // Acquire the wall: nudge forward to touch, square up, back off, and
        // turn to put the wall on the right side for WallFollow.
        driveForwardUntilTouch();
        Actions::Forward::forward(100, 60, /*useIMU=*/false, /*pumpComms=*/true);
        Actions::Forward::forward(-EXIT_BACKUP_SPEED, EXIT_BACKUP_MM, /*useIMU=*/false, /*pumpComms=*/true);
        Actions::Turn::turn(EXIT_TURN_ANGLE, EXIT_TURN_SPEED);
        Actions::Forward::forward(-80, 80, /*useIMU=*/false, /*pumpComms=*/true);
        Actions::WallFollow::reset();

        while (true) {
            Sensors::ToF::tick();      // WallFollow reads the tofFL grid
            Sensors::Touch::tick();    // WallFollow handles the bumper internally

            if (silverTapeSeen()) {
                // Silver = exit: push through the tape, back to line-follow.
                Actions::Drive::stop();
                tone(BUZZER_PIN, 8000, 30);
                Actions::Forward::forward(EXIT_PUSH_SPEED, EXIT_PUSH_MM,
                                          /*useIMU=*/false, /*pumpComms=*/true);
                Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
                Processing::XiaoDecode::clearFilter();
                StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
                return;
            }

            // Black tape is intentionally NOT checked -- it no longer ends evac.
            Actions::WallFollow::tick(EXIT_WALL_TARGET_MM, EXIT_WALL_BASE_SPEED,
                                      EXIT_WALL_FAR_MM, /*detectSudden=*/false,
                                      /*stopOnNoWall=*/false);
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
    // Nothing held yet -> drive with BOTH grippers open; scooping straight in
    // is far easier than opening at the last moment. Once a side holds, only
    // the still-empty side gets (re)opened by captureWithArm before its grab.
    Actions::Arm::grabLeft(false);
    Actions::Arm::grabRight(false);
    VictimManager::reset();
    // MODEL_VICTIMS (0x02) is the K230's default model slot, which now holds the
    // colordet colour model -- so this selects colour detection for the chase.
    Processing::K230Decode::setModel(Processing::K230Decode::MODEL_VICTIMS);
    Processing::K230Decode::setRunning(true);
    Actions::Forward::forward(80,100);
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
                // Still holding nothing (failed/aborted grab left a gripper
                // closed on air) -> back to the easy-scoop pose: both open.
                if (VictimManager::count() == 0) {
                    Actions::Arm::grabLeft(false);
                    Actions::Arm::grabRight(false);
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

    // Both grippers full. The captured colours are held in the arm (no bucket
    // deposit) and saved in VictimManager::leftColor()/rightColor() for later
    // use.
    Actions::Drive::stop();
    Serial.printf("[EVAC] grippers full  left=%s right=%s\n",
                  Processing::K230Decode::colorName(VictimManager::leftColor()),
                  Processing::K230Decode::colorName(VictimManager::rightColor()));
    DeployPlan::setFatenColors(VictimManager::leftColor(), VictimManager::rightColor());

    // Colours saved -> run the wall-follow exit: hunt the silver exit tape and
    // drive out to the line. setMode + the LINE_FOLLOW transition happen inside.
    runExitSequence();
}

}  // namespace EVAC
