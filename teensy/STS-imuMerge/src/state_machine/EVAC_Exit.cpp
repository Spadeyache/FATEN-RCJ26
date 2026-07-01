#include "EVAC_Exit.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../actions/Drive.h"
#include "../actions/Forward.h"
#include "../actions/Arm.h"
#include "../actions/Turn.h"
#include "../actions/WallFollow.h"
#include "../processing/XiaoDecode.h"
#include "../sensors/ToF.h"
#include "../sensors/Touch.h"
#include "../sensors/XIAO_link.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Exit
//
//  1. Wall-follow along the evacuation-zone wall (Actions::WallFollow).
//  2. Black tape means the exit is confirmed; push through and return to
//     LINE_FOLLOW, no matter when/where it shows up.
//  3. Silver means a wrong-exit marker; back off, turn, and let the normal
//     blind-search drive re-acquire the wall.
//  4. A sudden wall-loss (real opening, not just drift) pauses forward drive
//     for a fixed window while steering (PID) and sensors keep running, then
//     resumes; a gradual loss just keeps driving straight (WallFollow's own
//     blind-search behavior) until the wall, black, or silver shows up.
// =============================================================================

namespace EVAC_Exit {

namespace {
    // --- Deployment toggles ---------------------------------------------------
    constexpr bool     EXIT_DEBUG_PRINT_TOF      = false;
    constexpr uint32_t EXIT_DEBUG_LOOP_PAUSE_MS  = 0;

    // --- Wall-follow tuning --------------------------------------------------
    constexpr float WALL_TARGET_MM  = 100.0f;
    constexpr float WALL_BASE_SPEED = 60.0f;

    // --- Sudden-candidate pause ------------------------------------------------
    // Forward drive is disabled for this long; PID steering and touch/black
    // /silver monitoring all keep running (tick() is called with baseSpeed=0).
    constexpr uint32_t SUDDEN_PAUSE_MS = 10000;

    // --- Silver recovery -------------------------------------------------------
    constexpr float SILVER_PUSH_SPEED   = 100.0f;
    constexpr float SILVER_PUSH_MM      = 30.0f;
    constexpr float SILVER_BACKUP_MM    = 80.0f;
    constexpr float SILVER_BACKUP_SPEED = 50.0f;
    constexpr float SILVER_TURN_ANGLE   = -90.0f;
    constexpr float SILVER_TURN_SPEED   = 50.0f;

    // --- Exit confirmation -------------------------------------------------
    constexpr float BLACK_CONFIRM_SPEED = 50.0f;
    constexpr float BLACK_CONFIRM_MM    = 70.0f;

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

    void returnToLineFollow() {
        Actions::Drive::stop();
        tone(BUZZER_PIN, 9000, 4000);
        setDetectionLed(false);
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
    }

    // Exit confirmed: push through the line (touch is intentionally ignored —
    // this is the one case where we drive over/through no matter what) and
    // hand off to LINE_FOLLOW.
    void finishExit() {
        Serial.println("[EXIT] black confirmed -> LINE_FOLLOW");
        Actions::Drive::stop();
        Actions::Forward::forward(BLACK_CONFIRM_SPEED, BLACK_CONFIRM_MM);
        returnToLineFollow();
    }

    // Wrong-exit marker: back off, turn away from the wall, and reset
    // WallFollow. The next tick()s naturally blind-search forward until the
    // wall, black, or silver shows up again — no separate drive loop needed.
    void recoverFromSilver() {
        Serial.println("[EXIT] silver -> recover");
        Actions::Forward::forward(SILVER_PUSH_SPEED, SILVER_PUSH_MM);
        Actions::Forward::forward(-SILVER_BACKUP_SPEED, SILVER_BACKUP_MM);
        Actions::Turn::turn(SILVER_TURN_ANGLE, SILVER_TURN_SPEED);
        refreshTapeFlags();
        Actions::WallFollow::reset();
    }

    // A sudden wall-loss (likely a real opening, not just drift): disable
    // forward drive for a fixed window. PID steering keeps running (tick() is
    // called with baseSpeed=0, so any correction just pivots in place) and
    // touch/black/silver are all still watched. Only touch/black/silver end
    // the pause early; simply seeing the wall again does not - we wait out
    // the full window regardless, then let forward drive resume next tick().
    void handleSuddenPause() {
        Serial.println("[EXIT] sudden wall-loss -> 10s steer-only pause");
        const uint32_t end = millis() + SUDDEN_PAUSE_MS;
        while ((int32_t)(end - millis()) > 0) {
            refreshTapeFlags();
            if (s_tape.black) { finishExit(); return; }
            if (s_tape.silver) { recoverFromSilver(); return; }

            if (Actions::WallFollow::tick(WALL_TARGET_MM, 0.0f) ==
                Actions::WallFollow::Status::TOUCH) {
                Serial.println("[EXIT] touch during pause -> recovered");
                return;
            }
            delayMicroseconds(10000);
        }
        Serial.println("[EXIT] pause elapsed -> forward re-enabled");
    }
}  // namespace

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_EXIT");
#endif
    Actions::Drive::stop();
    Actions::Arm::grabLeft(true);
    Actions::Arm::grabRight(true);
    Actions::Arm::liftPark();

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
    //   silver -> wrong-exit marker
    refreshTapeFlags();

    // The exit line can appear under the camera at any time during wall-follow,
    // not only at a side-ToF opening. If black is seen, the exit is found, so
    // finish immediately instead of driving over it.
    if (s_tape.black) {
        finishExit();
        return;
    }

    if (s_tape.silver) {
        recoverFromSilver();
        return;
    }

    // Optional bench/debug hooks. In deployment both are off, so update() stays
    // responsive and wall-follow runs every state-machine tick.
    if (EXIT_DEBUG_LOOP_PAUSE_MS > 0) waitWithSensors(EXIT_DEBUG_LOOP_PAUSE_MS);
    if (EXIT_DEBUG_PRINT_TOF) Sensors::ToF::printFL();

    // WallFollow owns the motors: PID-follow while the wall is visible, blind
    // -search forward while it's gradually/never been found, or report a
    // sudden loss for us to pause on. Touch is handled internally by
    // WallFollow (TOUCH status) - nothing more to do here for that case.
    const auto status = Actions::WallFollow::tick(WALL_TARGET_MM, WALL_BASE_SPEED);
    if (status == Actions::WallFollow::Status::EXIT_CANDIDATE_SUDDEN) {
        handleSuddenPause();
    }
}

}  // namespace EVAC_Exit
