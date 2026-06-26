#include "Arm.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include <Arduino.h>
#include <Servo.h>

namespace Actions {
namespace Arm {

namespace {
    Servo _grabServo0;
    Servo _grabServo1;
    Servo _krs;            // KRS lift, PWM mode (pin = KRS_PWM_PIN)

    constexpr int KRS_PARK_US    = 1000;   // parked pose at boot — TODO tune
}

void attachGrabServos() {
    _grabServo0.attach(HS45HB0_PIN, 1000, 2000);
    _grabServo1.attach(HS45HB1_PIN, 1000, 2000);
}

void detachGrabServos() {
    _grabServo0.detach();
    _grabServo1.detach();
}

void init() {
    pinMode(BUZZER_PIN, OUTPUT);
    analogWrite(BUZZER_PIN, 0);

    attachGrabServos();
    grab(true);                          // open gripper on boot

    _krs.attach(KRS_PWM_PIN, 600, 2400);
    lift(KRS_PARK_US);                    // parked (raised)

    detachGrabServos();                   // silence the hobby servos
}

void grab(bool closed) {
    if (closed) {
        _grabServo0.writeMicroseconds(2000);
        _grabServo1.writeMicroseconds(1000);
    } else {
        _grabServo0.writeMicroseconds(1000);
        _grabServo1.writeMicroseconds(2000);
    }
}

namespace {
    // Close→reopen the gripper once to scoop a ball into storage. Blocking.
    void scoopOnce() {
        attachGrabServos();
        grab(true);   delay(300);   // close: pull the ball into storage
        grab(false);  delay(150);   // reopen: ready for the next ball
        detachGrabServos();
    }
}

// Phase 1: both routes drive the single gripper. TODO: actuate the dedicated
// black (dead) and silver (alive) arms separately once they are wired.
void captureDead()  { scoopOnce(); }
void captureAlive() { scoopOnce(); }

void releaseAll() {
    attachGrabServos();
    grab(false);  delay(300);       // open everything to drop held balls
    detachGrabServos();
}

// Move the lift to a PWM pulse width (microseconds) and HOLD it there.
// Blocking ~800 ms settle; the Servo library keeps refreshing the pulse after,
// so the lift holds its load (PWM mode has no "free" — detach to release).
void lift(int us) {
    us = constrain(us, 600, 2400);
    _krs.writeMicroseconds(us);
#if PRINT_ACTIONS
    Serial.print("KRS PWM lift -> "); Serial.println(us);
#endif
    delay(800);
}

}  // namespace Arm
}  // namespace Actions
