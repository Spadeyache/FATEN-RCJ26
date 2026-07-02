#include "EVAC_Entry.h"
#include "StateMachine.h"
#include "VictimManager.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/Touch.h"
#include "../sensors/ToF.h"
#include "../sensors/IMU.h"
#include "../sensors/XIAO_link.h"
#include "../actions/Drive.h"
#include "../actions/Turn.h"
#include "../actions/Forward.h"
#include "../actions/Arm.h"
#include "../actions/WallFollow.h"
#include "../processing/XiaoDecode.h"
#include "../processing/K230Decode.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Entry â€” fixed entry sequence into the evacuation zone.
//
//  This is the verbatim sequence ported from the original enterEvacuationZone():
//    1. Re-engage grab servos, raise arm, open gripper
//    2. Drive in, turn, drive across the zone, drop gripper, lift away
//    3. Hunt for the wall: drive forward until touchfront
//    4. Beep, back off, drive in again until touchfront, turn-and-deposit
//  5. Wall-follow (Actions::WallFollow) toward the first victim: while
//     neither a live nor dead ball is in view, PID-follow the wall when
//     close and blind-drive forward when it isn't (invalid or >= 200mm) -
//     no "sudden jump" pause here like EVAC_Exit, any loss just means keep
//     going. A tape-color marker (silver or black) gets the same back-off
//     -and-turn recovery touch gets, just with a longer backup. The moment
//     a victim is in view, hand off to EVAC_SEARCH_DEPLOY.
//
//  On completion: transitions to EVAC_SEARCH_DEPLOY.
//  All motions are blocking; this state runs once start-to-finish.
// =============================================================================

namespace EVAC_Entry {

namespace {
    constexpr float WALL_TARGET_MM  = 100.0f;
    constexpr float WALL_BASE_SPEED = 60.0f;
    constexpr float WALL_FAR_MM     = 200.0f;

    // Same back-off-and-turn recovery touch gets, but a bigger backup - a
    // tape marker means we're further past the wall than a bumped obstacle -
    // and a fixed turn angle (not the context-aware one touch uses): a
    // marker should always turn the same way, whether it fired mid-follow or
    // mid blind-search.
    constexpr float MARKER_BACKUP_MM = 80.0f;
    constexpr float MARKER_TURN_DEG  = -90.0f;

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

    bool markerSeen() {
        keepXiaoInEvacColorMaskMode();
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
        return Processing::XiaoDecode::silverSeen() ||
               Processing::XiaoDecode::evacBlackSeen();
    }

    bool victimInView() {
        Processing::K230Decode::tick();
        return Processing::K230Decode::checkVictim();
    }

    unsigned long s_startMs = 0;
    bool          s_skipSearch = false;  // global clock ran out before a victim ever showed up

    bool globalTimedOut() {
        return (unsigned long)(millis() - s_startMs) >= GLOBAL_TIMEOUT_MS;
    }

    // While no victim is in view, wall-follow toward one. This loop is
    // blocking (doesn't return to the main Arduino loop()), so it has to
    // re-tick Touch/ToF itself the same way EVAC_Exit's blocking loops do.
    // Returns true once a victim is in view; false if the global evac clock
    // ran out first.
    bool huntForVictim() {
        Serial.println("[ENTRY] hunting for victim along wall");
        setDetectionLed(true);
        while (!victimInView()) {
            if (globalTimedOut()) {
                Actions::Drive::stop();
                setDetectionLed(false);
                Serial.println("[ENTRY] global evac clock ran out -> skip search, go to EXIT");
                return false;
            }

            Sensors::Touch::tick();
            Sensors::ToF::tick();

            if (markerSeen()) {
                Serial.println("[ENTRY] marker seen -> recover");
                Actions::WallFollow::recover(MARKER_BACKUP_MM, MARKER_TURN_DEG);
                continue;
            }

            // detectSudden=false: no EVAC_Exit-style pause here, any far
            // /invalid reading just means keep driving forward.
            Actions::WallFollow::tick(WALL_TARGET_MM, WALL_BASE_SPEED,
                                       WALL_FAR_MM, /*detectSudden=*/false);
            delayMicroseconds(10000);
        }
        Actions::Drive::stop();
        setDetectionLed(false);
        Serial.println("[ENTRY] victim in view -> search");
        return true;
    }
}  // namespace

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_ENTRY");
#endif

    // Mark evac zone entry: start of the 3:00 global clock (see GLOBAL_TIMEOUT_MS).
    s_startMs = millis();

    // Fresh evac run: clear held counts.
    VictimManager::reset();
    digitalWrite(LED_BUILTIN, LOW);
    pinMode(LED_BUILTIN, OUTPUT);

    Actions::Arm::attachServos();

    Actions::Forward::forward(62, 100, /*useIMU=*/false, /*pumpComms=*/true);

    Actions::Turn::turn(-40);
    Actions::Forward::forward(62, 70, /*useIMU=*/false, /*pumpComms=*/true);
    Actions::Drive::stop();
    Actions::WallFollow::reset();
    s_skipSearch = !huntForVictim();
}

void update() {
    // onEnter() ran the entire entry sequence, including the wall-follow
    // hunt. If the global clock already ran out before a victim ever showed
    // up, there's no time budget left for search/deploy either - go
    // straight to EVAC_EXIT. Otherwise hand off to search+deploy as usual.
    StateMachine::transitionTo(s_skipSearch ? StateMachine::EVAC_EXIT
                                             : StateMachine::EVAC_SEARCH_DEPLOY);
}

unsigned long startMs() { return s_startMs; }

}  // namespace EVAC_Entry
