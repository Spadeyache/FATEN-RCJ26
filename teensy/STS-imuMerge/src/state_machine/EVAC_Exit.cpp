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
//  4. ANY wall loss (ToF >= 200mm or invalid): stop, then probe the gap -
//     fwd 150mm, turn +90 into it, fwd 210mm, checking silver/black between
//     every step (LED high throughout). Silver/bump -> back 110mm, turn -90,
//     resume wall-follow. Black -> push 70mm, spin with no angle limit until
//     the line's black center point is seen (obstacle-style finish), then
//     LINE_FOLLOW. Nothing -> resume the wall-follow PID loop.
//  5. Front touch mid-follow: short push to square up on the wall, then
//     back off and turn away (same distances as the on-enter maneuver).
// =============================================================================

namespace EVAC_Exit {

namespace {
    // --- Deployment toggles ---------------------------------------------------
    constexpr bool     EXIT_DEBUG_PRINT_TOF      = false;
    constexpr uint32_t EXIT_DEBUG_LOOP_PAUSE_MS  = 0;

    // --- Wall-follow tuning --------------------------------------------------
    constexpr float WALL_TARGET_MM  = 100.0f;
    constexpr float WALL_BASE_SPEED = 65.0f;
    constexpr float WALL_FAR_MM     = 200.0f;  // ToF >= this (or -1) -> park + LED high

    // --- Mid-follow touch recovery ---------------------------------------------
    // Short push to square up on the wall, then the same back-off + turn the
    // on-enter maneuver uses.
    constexpr float TOUCH_SQUARE_UP_SPEED = 100.0f;
    constexpr float TOUCH_SQUARE_UP_MM    = 30.0f;
    constexpr float TOUCH_BACKUP_MM       = 50.0f;   // mid-follow touch: back this much before the turn

    // --- On-enter wall-acquisition maneuver ------------------------------------
    // Starting pose entering EXIT is unknown relative to any wall: nudge
    // forward in small steps until the front bumper finds one, back off to a
    // known standoff, then turn to put that wall on the right side for
    // WallFollow to pick up.
    constexpr float EXIT_ON_ENTER_FORWARD_SPEED   = 100.0f;
    constexpr float EXIT_ON_ENTER_FORWARD_STEP_MM = 40.0f;
    constexpr float EXIT_ON_ENTER_BACKUP_MM       = 40.0f;
    constexpr float EXIT_ON_ENTER_BACKUP_SPEED    = 70.0f;
    constexpr float EXIT_ON_ENTER_TURN_ANGLE      = -90.0f;
    constexpr float EXIT_ON_ENTER_TURN_SPEED      = 65.0f;

    // --- Sudden-candidate gap probe ---------------------------------------------
    // A sudden wall-loss is a likely real opening: turn into it and drive
    // forward (no fixed distance) watching black/silver/touch. Silver and
    // touch back off different distances - a marker means we're already well
    // past the wall's edge, a bump is just the near obstacle right there.
    constexpr float SUDDEN_TURN_ANGLE          = 90.0f;   // turn into the gap
    constexpr float SUDDEN_TURN_SPEED          = 60.0f;
    constexpr float SUDDEN_DRIVE_SPEED         = 60.0f;   // forward speed while probing
    constexpr float SUDDEN_TOUCH_BACKUP_MM     = 40.0f;
    constexpr float SUDDEN_SILVER_BACKUP_MM    = 120.0f;
    constexpr float SUDDEN_RECOVER_BACKUP_SPEED = 50.0f;
    constexpr float SUDDEN_RECOVER_TURN_ANGLE  = -90.0f;
    constexpr float SUDDEN_RECOVER_TURN_SPEED  = 50.0f;

    // --- Wall-loss gap probe (replaces the plain park) ---------------------------
    // ToF >= WALL_FAR_MM or -1 -> probe the opening: fwd 150, turn +90 (gap is
    // on the wall side), fwd 210 - checking silver/black between every step.
    //   silver -> back 110, turn -90, resume wall-follow
    //   black  -> push 70, then spin with NO angle limit until the line's black
    //             center point is seen (obstacle-style finish) -> LINE_FOLLOW
    //   none   -> maneuver done, fall back into the wall-follow PID loop
    constexpr float PROBE_SPEED           = 60.0f;
    constexpr float PROBE_STEP_MM         = 30.0f;   // check tape between steps
    constexpr float PROBE_FWD1_MM         = 150.0f;
    constexpr float PROBE_TURN_DEG        = 90.0f;
    constexpr float PROBE_TURN_SPEED      = 60.0f;
    constexpr float PROBE_FWD2_MM         = 300.0f;
    constexpr float PROBE_BLACK_PUSH_MM   = 70.0f;
    constexpr float PROBE_LINE_SPIN_SPEED = 45.0f;   // same as obstacle finish
    // Post-silver / post-probe re-acquire: drive forward until the FRONT view
    // sees something closer than this - read from the 2nd row from the top of
    // the 8x8 ToF grid, its two centre cells. (Flip ROW to 6 if the grid turns
    // out to be mounted upside down.)
    constexpr float   FRONT_NEAR_MM     = 200.0f;
    constexpr uint8_t FRONT_NEAR_ROW    = 1;   // 2nd row from the top
    constexpr uint8_t FRONT_NEAR_COL_LO = 3;   // centre two cells
    constexpr uint8_t FRONT_NEAR_COL_HI = 4;

    // --- Silver recovery -------------------------------------------------------
    constexpr float SILVER_PUSH_SPEED   = 100.0f;
    constexpr float SILVER_PUSH_MM      = 30.0f;
    constexpr float SILVER_BACKUP_MM    = 115.0f;
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
        // LED is NOT forced high here: in EXIT it stays LOW while following
        // and goes HIGH only while parked on a far/invalid ToF (see update()).
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

    void forwardUntilWallNear();   // defined below (probe section)

    // Wrong-exit marker: back off, turn away from the wall, then drive
    // forward until the wall ToF reads close again. Without that close-in, a
    // far/-1 reading right after the turn would immediately fire the gap probe.
    void recoverFromSilver() {
        Serial.println("[EXIT] silver -> recover");
        // Actions::Forward::forward(SILVER_PUSH_SPEED, SILVER_PUSH_MM);
        Actions::Forward::forward(-SILVER_BACKUP_SPEED, SILVER_BACKUP_MM);
        Actions::Turn::turn(SILVER_TURN_ANGLE, SILVER_TURN_SPEED);
        Actions::Forward::forward(100, 200);
        refreshTapeFlags();
        Actions::WallFollow::reset();
        forwardUntilWallNear();
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

    // --- Wall-loss gap probe -----------------------------------------------

    enum class ProbeSeen { NONE, SILVER, BLACK, TOUCH };

    // Drive `mm` forward in PROBE_STEP_MM chunks, refreshing silver/black
    // (and touch, as a crash guard) between chunks. Stops early on a hit.
    ProbeSeen probeForward(float mm) {
        float remaining = mm;
        while (remaining > 0.0f) {
            const float step = remaining < PROBE_STEP_MM ? remaining : PROBE_STEP_MM;
            Actions::Forward::forward(PROBE_SPEED, step);
            remaining -= step;

            refreshTapeFlags();
            if (s_tape.black)  return ProbeSeen::BLACK;
            if (s_tape.silver) return ProbeSeen::SILVER;
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) return ProbeSeen::TOUCH;
        }
        return ProbeSeen::NONE;
    }

    // Black during the probe: we're on the exit line but pointed out of the
    // gap, not along the line. Push through, then spin with NO angle limit
    // (obstacle-style finish) until the line's black center point is seen,
    // and hand off to LINE_FOLLOW.
    void finishExitWithLineSpin() {
        Serial.println("[EXIT] black in probe -> push through + spin to line");
        Actions::Drive::stop();
        Actions::Forward::forward(BLACK_CONFIRM_SPEED, PROBE_BLACK_PUSH_MM);
        // Exactly the obstacle end sequence: CENTER_POINT mode + pump, then
        // spin until FEAT_CENTER_POINT_BLACK (the same flag obstacle stops
        // on). Mode re-sent every retry in case the command is dropped.
        // "No angle limit": loop the timeout version until the point is found.
        do {
            Processing::XiaoDecode::setMode(XIAO_MODE_CENTER_POINT);
            const uint32_t pumpStart = millis();
            while (millis() - pumpStart < 60) {
                Sensors::XIAO_link::tick();
                Processing::XiaoDecode::tick(true);
            }
        } while (!Actions::Turn::turnUntilCenterPoint(1.0f, PROBE_LINE_SPIN_SPEED, 10000UL));
        returnToLineFollow();
    }

    // True when either centre cell of the 2nd-from-top ToF row reads valid
    // and closer than FRONT_NEAR_MM.
    bool frontRowNear() {
        for (uint8_t col = FRONT_NEAR_COL_LO; col <= FRONT_NEAR_COL_HI; col++) {
            const int16_t mm = tofFL[FRONT_NEAR_ROW][col];
            if (mm >= 0 && (float)mm < FRONT_NEAR_MM) return true;
        }
        return false;
    }

    // Post-silver / post-probe re-acquire: drive forward until the FRONT view
    // (2nd-from-top ToF row) sees something closer than FRONT_NEAR_MM.
    // Aborts on black/silver/touch and just stops - update() re-runs right
    // after and its own handlers take those cases.
    // Blocking, so it ticks ToF/Touch itself (main loop isn't running).
    void forwardUntilWallNear() {
        Serial.println("[EXIT] re-acquire: forward until front row < 200mm");
        // tofFL is STALE right after a back-off/turn (tick() only writes on a
        // fresh frame) - require two fresh frames before trusting the grid,
        // or the pre-turn wall reading would end this drive instantly.
        uint8_t freshFrames = 0;
        while (true) {
            if (Sensors::ToF::tick() && freshFrames < 2) freshFrames++;
            if (freshFrames >= 2 && frontRowNear()) break;

            refreshTapeFlags();
            if (s_tape.black || s_tape.silver) break;
            Sensors::Touch::tick();
            if (Sensors::Touch::front()) break;

            Actions::Drive::motor(PROBE_SPEED, PROBE_SPEED);
            delayMicroseconds(10000);
        }
        Actions::Drive::stop();
        Actions::WallFollow::reset();
    }

    // The whole wall-loss maneuver: fwd 150 -> turn +90 -> fwd 210, watching
    // silver/black throughout. Returns having either handled an exit line
    // (state transition), backed out (silver/touch), or finished cleanly -
    // in which case the caller just falls back into the wall-follow loop.
    void probeGap() {
        Serial.println("[EXIT] wall lost -> probe: fwd 150, turn 90, fwd 210");
        setDetectionLed(true);   // HIGH for the whole out-of-wall maneuver

        ProbeSeen seen = probeForward(PROBE_FWD1_MM);
        if (seen == ProbeSeen::NONE) {
            Actions::Turn::turn(PROBE_TURN_DEG, PROBE_TURN_SPEED);
            refreshTapeFlags();
            if      (s_tape.black)  seen = ProbeSeen::BLACK;
            else if (s_tape.silver) seen = ProbeSeen::SILVER;
            else                    seen = probeForward(PROBE_FWD2_MM);
        }

        if (seen == ProbeSeen::BLACK) {
            finishExitWithLineSpin();      // transitions to LINE_FOLLOW
            return;
        }
        if (seen == ProbeSeen::SILVER) {
            Serial.println("[EXIT] silver in probe -> back out");
            recoverFromSilver();   // SAME sequence as mid-follow silver
            return;
        }
        if (seen == ProbeSeen::TOUCH) {
            // Just stop: the bumper is still pressed, so the next update()
            // runs the ONE touch handler (push 30 / back 50 / turn -90).
            Serial.println("[EXIT] touch in probe -> hand to touch handler");
            Actions::Drive::stop();
            return;
        }

        // Nothing seen: maneuver complete - close in on the wall (<= 180mm),
        // then resume the wall-follow PID loop.
        forwardUntilWallNear();
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
    // tone(BUZZER_PIN, 9000, 6000);  // big audible cue: entering EVAC_EXIT

    Actions::Drive::stop();
    Actions::Arm::grabLeft(true);
    Actions::Arm::grabRight(true);
    Actions::Arm::liftPark();

    Processing::XiaoDecode::setMode(XIAO_MODE_EVAC_COLOR_MASK);
    driveForwardUntilTouch();
    Actions::Forward::forward(100, 60);
    Actions::Forward::forward(-EXIT_ON_ENTER_BACKUP_SPEED, 50);
    Actions::Turn::turn(EXIT_ON_ENTER_TURN_ANGLE, EXIT_ON_ENTER_TURN_SPEED);
    Actions::Forward::forward(-80, 80);

    Sensors::Touch::tick();
    pinMode(LED_BUILTIN, OUTPUT);
    setDetectionLed(false);   // LOW in exit; HIGH only while parked on wall loss
    
    refreshTapeFlags();
    Actions::WallFollow::reset();
    Sensors::Touch::tick();
    Sensors::Touch::tick();
    Sensors::Touch::tick();
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
        tone(BUZZER_PIN, 8000,30);
        finishExit();
        return;
    }

    if (s_tape.silver) {
        tone(BUZZER_PIN, 8000,30);
        recoverFromSilver();
        return;
    }


    // Front touch: square up on the wall with a short push, then back off and
    // turn away. Checked HERE (before WallFollow::tick) so this sequence wins
    // over WallFollow's internal 48mm/-40° recovery.
    Sensors::Touch::tick();
    if (Sensors::Touch::front()) {
        Serial.println("[EXIT] touch -> square up, back off, turn");
        Actions::Drive::stop();
        Actions::Forward::forward(TOUCH_SQUARE_UP_SPEED, TOUCH_SQUARE_UP_MM);
        Actions::Forward::forward(-EXIT_ON_ENTER_BACKUP_SPEED, TOUCH_BACKUP_MM);
        Actions::Turn::turn(EXIT_ON_ENTER_TURN_ANGLE, EXIT_ON_ENTER_TURN_SPEED);
        Actions::WallFollow::reset();
        return;
    }

    // Optional bench/debug hooks. In deployment both are off, so update() stays
    // responsive and wall-follow runs every state-machine tick.
    // if (EXIT_DEBUG_LOOP_PAUSE_MS > 0) waitWithSensors(EXIT_DEBUG_LOOP_PAUSE_MS);
    // if (EXIT_DEBUG_PRINT_TOF) Sensors::ToF::printFL();

    // Wall PID while the wall is visible. ToF >= WALL_FAR_MM (200mm) or -1
    // (invalid) -> stop, then run the gap probe (fwd 150 / turn +90 / fwd 210,
    // silver/black checked throughout - see probeGap()). If the probe finds
    // nothing it returns here and the PID wall-follow loop resumes as-is.
    // LED: LOW while following, HIGH for the whole probe maneuver.
    const auto status = Actions::WallFollow::tick(WALL_TARGET_MM, WALL_BASE_SPEED,
                                                  WALL_FAR_MM,
                                                  /*detectSudden=*/false,
                                                  /*stopOnNoWall=*/true);
    setDetectionLed(status != Actions::WallFollow::Status::FOLLOWING);
    if (status == Actions::WallFollow::Status::NO_WALL) {
        probeGap();
        setDetectionLed(false);
    }
}

}  // namespace EVAC_Exit
