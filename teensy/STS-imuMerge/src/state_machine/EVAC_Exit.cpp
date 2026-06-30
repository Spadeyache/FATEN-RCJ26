#include "EVAC_Exit.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../actions/Drive.h"
#include "../actions/Forward.h"
#include "../actions/Turn.h"
#include "../actions/WallFollow.h"
#include "../processing/XiaoDecode.h"
#include "../sensors/ToF.h"
#include "../sensors/Touch.h"
#include "../sensors/XIAO_link.h"

#include <Arduino.h>
#include <math.h>

// =============================================================================
//  EVAC_Exit
//
//  1. Wall-follow along the evacuation-zone wall.
//  2. When WallFollow reports an exit candidate, check XIAO evac color-mask.
//  3. Black tape means the exit is confirmed; return to LINE_FOLLOW.
//  4. Otherwise approach, turn into the candidate gap, ram, and confirm/recover.
// =============================================================================

namespace EVAC_Exit {

namespace {
    // --- Deployment toggles ---------------------------------------------------
    constexpr bool     EXIT_DEBUG_PRINT_TOF      = false;
    constexpr uint32_t EXIT_DEBUG_LOOP_PAUSE_MS  = 0;

    // --- Wall-follow tuning --------------------------------------------------
    constexpr float WALL_TARGET_MM  = 100.0f;
    constexpr float WALL_BASE_SPEED = 60.0f;

    // --- Exit-confirmation motion tuning -------------------------------------
    constexpr float RAM_SPEED       = 50.0f;
    constexpr float RAM_APPROACH_MM = 160.0f;  // forward before the right turn
    constexpr float RAM_DISTANCE_MM = 250.0f;  // drive into candidate gap
    constexpr float RAM_BACKUP_MM   = 60.0f;
    constexpr float RAM_TURN_SPEED  = 50.0f;

    struct TapeFlags {
        bool silver = false;
        bool black = false;
    };

    TapeFlags s_tape;

    void setDetectionLed(bool on) {
        digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
    }

    void keepXiaoInEvacColorMaskMode() {
        static uint32_t lastModeSend = 0;
        const uint32_t now = millis();
        if (now - lastModeSend < 100) return;

        Processing::XiaoDecode::setMode(XIAO_MODE_EVAC_COLOR_MASK);
        lastModeSend = now;
    }

    TapeFlags readTapeFlags() {
        setDetectionLed(true);
        keepXiaoInEvacColorMaskMode();
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
        return {
            Processing::XiaoDecode::silverSeen(),
            Processing::XiaoDecode::evacBlackSeen(),
        };
    }

    void refreshTapeFlags() {
        s_tape = readTapeFlags();
    }

    void serviceSensors() {
        Sensors::Touch::tick();
        refreshTapeFlags();
    }

    void waitWithSensors(uint32_t ms) {
        const uint32_t end = millis() + ms;
        while ((int32_t)(end - millis()) > 0) {
            serviceSensors();
            if (Sensors::Touch::front()) Actions::Drive::stop();
            delayMicroseconds(10000);
        }
    }

    enum class DriveWatchResult {
        DONE,
        TOUCH,
        BLACK,
    };

    DriveWatchResult driveStraightAndWatch(float speed, float distanceMm,
                                           bool stopOnBlack) {
        const unsigned long duration = (unsigned long)
            fabsf(distanceMm * FORWARD_MS_PER_MM * MAX_MOTOR_SPEED / fabsf(speed));
        const unsigned long start = millis();
        unsigned long lastComms = 0;

        Serial.printf("[EXIT] drive %.0fmm @ %.0f (%lums)\n",
                      distanceMm, speed, duration);

        Actions::Drive::motor(speed, speed);
        while (millis() - start < duration) {
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) {
                Actions::Drive::stop();
                Serial.println("[EXIT] touch during drive");
                return DriveWatchResult::TOUCH;
            }

            if (millis() - lastComms >= 20) {
                refreshTapeFlags();
                if (stopOnBlack && s_tape.black) {
                    Actions::Drive::stop();
                    Serial.println("[EXIT] black seen during approach");
                    return DriveWatchResult::BLACK;
                }
                lastComms = millis();
            }
        }

        Actions::Drive::stop();
        refreshTapeFlags();
        return DriveWatchResult::DONE;
    }

    void returnToLineFollow() {
        Actions::Drive::stop();
        tone(BUZZER_PIN, 9000, 4000);
        setDetectionLed(false);
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
    }

    void recoverToWallFollow() {
        Serial.printf("[EXIT] recover: back %.0fmm, left 90\n", RAM_BACKUP_MM);
        Actions::Forward::forward(-RAM_SPEED, RAM_BACKUP_MM);
        Actions::Turn::turn(-90.0f, RAM_TURN_SPEED);
        refreshTapeFlags();
        Actions::WallFollow::reset();
    }

    bool finishIfBlackSeen(const char* where) {
        refreshTapeFlags();
        if (!s_tape.black) return false;

        Serial.printf("[EXIT] black confirmed at %s -> LINE_FOLLOW\n", where);
        returnToLineFollow();
        return true;
    }

    bool approachCandidateOpening() {
        Serial.printf("[EXIT] approach %.0fmm before turn\n", RAM_APPROACH_MM);
        waitWithSensors(1000);
        const DriveWatchResult result =
            driveStraightAndWatch(RAM_SPEED, RAM_APPROACH_MM, true);

        if (result == DriveWatchResult::BLACK) {
            returnToLineFollow();
            return true;
        }
        if (result == DriveWatchResult::TOUCH) {
            recoverToWallFollow();
            return true;
        }
        return false;
    }

    DriveWatchResult ramCandidateOpening() {
        Serial.println("[EXIT] turn right 90 into candidate");
        waitWithSensors(1000);
        Actions::Turn::turn(90.0f, RAM_TURN_SPEED);

        Serial.printf("[EXIT] ram %.0fmm\n", RAM_DISTANCE_MM);
        waitWithSensors(1000);
        // Watch black for the whole straight ram, not just at the end, so a line
        // crossed mid-ram is caught immediately.
        const DriveWatchResult result =
            driveStraightAndWatch(RAM_SPEED, RAM_DISTANCE_MM, true);
        if (result == DriveWatchResult::BLACK) return result;
        waitWithSensors(500);
        return result;
    }

    void handleRamResult(bool hitSomething) {
        refreshTapeFlags();

        if (hitSomething) {
            Serial.println("[EXIT] ram hit wall/obstacle -> recover");
            waitWithSensors(1000);
            recoverToWallFollow();
            return;
        }

        if (s_tape.silver) {
            Serial.println("[EXIT] silver/wrong exit -> recover");
            waitWithSensors(1000);
            recoverToWallFollow();
            return;
        }

        if (finishIfBlackSeen("ram")) return;

        Serial.println("[EXIT] no color confirmed -> recover");
        waitWithSensors(1000);
        recoverToWallFollow();
    }

    void handleExitCandidate() {
        if (finishIfBlackSeen("candidate")) return;

        // Silver is intentionally ignored here for now; after the ram it is
        // treated as a wrong exit and recovered.
        if (approachCandidateOpening()) return;
        const DriveWatchResult ramResult = ramCandidateOpening();
        if (ramResult == DriveWatchResult::BLACK) {
            returnToLineFollow();
            return;
        }
        handleRamResult(ramResult == DriveWatchResult::TOUCH);
    }
}  // namespace

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_EXIT");
#endif
    pinMode(LED_BUILTIN, OUTPUT);
    setDetectionLed(true);
    Processing::XiaoDecode::setMode(XIAO_MODE_EVAC_COLOR_MASK);
    refreshTapeFlags();
    Actions::WallFollow::reset();
}

void update() {
    // Keep the latest XIAO evac color-mask flags cached before making decisions.
    // Mode 5 reports:
    //   black  -> correct exit line found
    //   silver -> wrong-exit marker, handled after the ram check
    refreshTapeFlags();

    // The exit line can appear under the camera at any time during wall-follow,
    // not only at a side-ToF opening. If black is seen, the exit is found, so
    // return to line follow immediately instead of driving over it.
    if (s_tape.black) {
        Serial.println("[EXIT] black seen during wall-follow -> LINE_FOLLOW");
        returnToLineFollow();
        return;
    }

    // Silver during wall-follow = wrong-exit marker: back up and turn away,
    // same recover as after a ram.
    if (s_tape.silver) {
        Serial.println("[EXIT] silver seen during wall-follow -> recover");
        recoverToWallFollow();
        return;
    }

    // Optional bench/debug hooks. In deployment both are off, so update() stays
    // responsive and wall-follow runs every state-machine tick.
    if (EXIT_DEBUG_LOOP_PAUSE_MS > 0) waitWithSensors(EXIT_DEBUG_LOOP_PAUSE_MS);
    if (EXIT_DEBUG_PRINT_TOF) Sensors::ToF::printFL();

    // Normal behavior: WallFollow owns the motors while it sees the wall.
    // If the side ToF suddenly sees far/invalid space after a stable wall, that
    // is a potential exit opening, so we pause wall-follow and verify with XIAO.
    const auto status = Actions::WallFollow::tick(WALL_TARGET_MM, WALL_BASE_SPEED);
    if (status == Actions::WallFollow::Status::EXIT_CANDIDATE) {
        // Verification flow:
        //   1. If black is already visible, finish and return to LINE_FOLLOW.
        //   2. Otherwise drive forward while watching XIAO for black.
        //   3. If still not confirmed, turn/ram into the candidate and decide
        //      whether to recover or return to LINE_FOLLOW.
        handleExitCandidate();
    }
}

}  // namespace EVAC_Exit
