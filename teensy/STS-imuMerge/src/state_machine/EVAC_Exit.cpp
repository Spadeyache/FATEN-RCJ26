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
//  0. On enter: unknown starting pose, so nudge forward until the bumper
//     finds a wall, back off, and turn to put it on the right (ToF) side.
//  1. Wall-follow along the evacuation-zone wall (Actions::WallFollow).
//  2. Black tape means the exit is confirmed; push through and return to
//     LINE_FOLLOW, no matter when/where it shows up.
//  3. Silver means a wrong-exit marker; back off, turn, and let the normal
//     blind-search drive re-acquire the wall.
//  4. A sudden wall-loss (real opening, not just drift): turn into it and
//     drive forward, watching black/silver/touch, until one of them fires -
//     a gradual loss just keeps driving straight instead (WallFollow's own
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

    // --- On-enter wall-acquisition maneuver ------------------------------------
    // Starting pose entering EXIT is unknown relative to any wall: nudge
    // forward in small steps until the front bumper finds one, back off to a
    // known standoff, then turn to put that wall on the right side for
    // WallFollow to pick up.
    constexpr float EXIT_ON_ENTER_FORWARD_SPEED   = 100.0f;
    constexpr float EXIT_ON_ENTER_FORWARD_STEP_MM = 40.0f;
    constexpr float EXIT_ON_ENTER_BACKUP_MM       = 70.0f;
    constexpr float EXIT_ON_ENTER_BACKUP_SPEED    = 50.0f;
    constexpr float EXIT_ON_ENTER_TURN_ANGLE      = -90.0f;
    constexpr float EXIT_ON_ENTER_TURN_SPEED      = 60.0f;

    // --- Sudden-candidate gap probe ---------------------------------------------
    // A sudden wall-loss is a likely real opening: turn into it and drive
    // forward (no fixed distance) watching black/silver/touch. Silver and
    // touch back off different distances - a marker means we're already well
    // past the wall's edge, a bump is just the near obstacle right there.
    constexpr float SUDDEN_TURN_ANGLE          = 90.0f;   // turn into the gap
    constexpr float SUDDEN_TURN_SPEED          = 60.0f;
    constexpr float SUDDEN_DRIVE_SPEED         = 60.0f;   // forward speed while probing
    constexpr float SUDDEN_TOUCH_BACKUP_MM     = 70.0f;
    constexpr float SUDDEN_SILVER_BACKUP_MM    = 120.0f;
    constexpr float SUDDEN_RECOVER_BACKUP_SPEED = 50.0f;
    constexpr float SUDDEN_RECOVER_TURN_ANGLE  = -90.0f;
    constexpr float SUDDEN_RECOVER_TURN_SPEED  = 50.0f;

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

    // Silver or touch while probing a sudden-candidate gap: back off (a
    // marker means we're already past the wall's edge, so a bigger backup
    // than a plain bump) and turn back out of the gap.
    void recoverFromSuddenCandidate(float backupMm) {
        Actions::Drive::stop();
        Actions::Forward::forward(-SUDDEN_RECOVER_BACKUP_SPEED, backupMm);
        Actions::Turn::turn(SUDDEN_RECOVER_TURN_ANGLE, SUDDEN_RECOVER_TURN_SPEED);
        refreshTapeFlags();
        Actions::WallFollow::reset();
    }

    // A sudden wall-loss (likely a real opening, not just drift): turn into
    // it and drive forward - no fixed distance, just watch black/silver
    // /touch until one of them fires.
    void handleSuddenCandidate() {
        Serial.println("[EXIT] sudden wall-loss -> turn into gap and probe");
        Actions::Turn::turn(SUDDEN_TURN_ANGLE, SUDDEN_TURN_SPEED);

        Actions::Drive::motor(SUDDEN_DRIVE_SPEED, SUDDEN_DRIVE_SPEED);
        while (true) {
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) {
                Serial.println("[EXIT] touch while probing gap -> recover");
                recoverFromSuddenCandidate(SUDDEN_TOUCH_BACKUP_MM);
                return;
            }

            refreshTapeFlags();
            if (s_tape.black) {
                finishExit();
                return;
            }
            if (s_tape.silver) {
                Serial.println("[EXIT] silver while probing gap -> recover");
                recoverFromSuddenCandidate(SUDDEN_SILVER_BACKUP_MM);
                return;
            }
        }
    }

    // Nudge forward until the front bumper finds a wall. Small steps (not one
    // long blind drive) so it doesn't slam into the wall at full speed.
    void driveForwardUntilTouch() {
        Sensors::Touch::tick();
        while (!Sensors::Touch::front()) {
            Actions::Forward::forward(EXIT_ON_ENTER_FORWARD_SPEED, EXIT_ON_ENTER_FORWARD_STEP_MM);
            Sensors::Touch::tick();
        }
    }
}  // namespace

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_EXIT");
#endif
    tone(BUZZER_PIN, 9000, 6000);  // big audible cue: entering EVAC_EXIT

    Actions::Drive::stop();
    Actions::Arm::grabLeft(true);
    Actions::Arm::grabRight(true);
    Actions::Arm::liftPark();

    driveForwardUntilTouch();
    Actions::Forward::forward(-EXIT_ON_ENTER_BACKUP_SPEED, EXIT_ON_ENTER_BACKUP_MM);
    Actions::Turn::turn(EXIT_ON_ENTER_TURN_ANGLE, EXIT_ON_ENTER_TURN_SPEED);

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
        handleSuddenCandidate();
    }
}

}  // namespace EVAC_Exit
