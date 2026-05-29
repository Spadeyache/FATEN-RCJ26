#include "EVAC_Entry.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/Touch.h"
#include "../sensors/IMU.h"
#include "../actions/Drive.h"
#include "../actions/Turn.h"
#include "../actions/Forward.h"
#include "../actions/Arm.h"

#include <Arduino.h>

// =============================================================================
//  EVAC_Entry â€” fixed entry sequence into the evacuation zone.
//
//  This is the verbatim sequence ported from the original enterEvacuationZone():
//    1. Re-engage grab servos, raise arm, open gripper
//    2. Drive in, turn, drive across the zone, drop gripper, lift away
//    3. Hunt for the wall: drive forward until touchfront
//    4. Beep, back off, drive in again until touchfront, turn-and-deposit
//
//  On completion: transitions to EVAC_SEARCH.
//  All motions are blocking; this state runs once start-to-finish.
// =============================================================================

namespace EVAC_Entry {

void onEnter() {
#if PRINT_STATE
    Serial.println("State: EVAC_ENTRY");
#endif

    Actions::Arm::attachGrabServos();
    Actions::Arm::lift(4050);
    Actions::Arm::grab(true);

    Actions::Forward::forward(190, 100);
    Actions::Turn::turn(-80.0f);
    Actions::Forward::forward(100, 1150);
    Actions::Drive::stop();

    Actions::Arm::grab(false);
    Actions::Arm::lift(10050);
    Actions::Arm::lift(10050);   // intentional duplicate â€” KRS needs the resend

    Actions::Turn::turn(82.0f);
    Actions::Forward::forward(-100, 450);
    Actions::Turn::turn(120.0f);

    // Crawl forward until front bumper hits the wall.
    Sensors::IMU::tick();
    Actions::Drive::motor(70, 70);
    while (!Sensors::Touch::front()) {
        delay(10);
        Sensors::IMU::tick();
        Sensors::Touch::tick();
    }

    Actions::Arm::lift(7050);

    analogWrite(BUZZER_PIN, 160);
    Actions::Forward::forward(-50, 50);

    Actions::Drive::motor(70, 70);
    while (!Sensors::Touch::front()) {
        delay(10);
        Sensors::Touch::tick();
    }
    Actions::Drive::stop();

    Actions::Turn::turn(-45.0f);
    Actions::Forward::forward(50, 40);
    Actions::Arm::grab(true);

    Actions::Drive::stop();
}

void update() {
    // onEnter() ran the entire entry sequence. Hand off to search.
    StateMachine::transitionTo(StateMachine::EVAC_SEARCH);
}

}  // namespace EVAC_Entry
