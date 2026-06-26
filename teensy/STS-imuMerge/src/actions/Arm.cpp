#include "Arm.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include <Arduino.h>
#include <Servo.h>

namespace Actions {
namespace Arm {

namespace {
    Servo _hs45hb0;
    Servo _hs45hb1;
    Servo _krs;            // KRS lift, PWM mode (pin = KRS_PWM_PIN)

    constexpr int KRS_PARK_US    = 1000;   // parked pose at boot — TODO tune
    constexpr int KRS_GRAB_US    = 2000;
    constexpr int KRS_AIR_US    = 1700;

    constexpr int HS0_CLOSE_US = 2000;
    constexpr int HS0_OPEN_US  = 1000;
    constexpr int HS1_CLOSE_US = 1000;
    constexpr int HS1_OPEN_US  = 2000;
}

void attachServos() {
    _hs45hb0.attach(HS45HB0_PIN, 1000, 2000);
    _hs45hb1.attach(HS45HB1_PIN, 1000, 2000);
    
    _krs.attach(KRS_PWM_PIN, 600, 2400);
}

void detachServos() {
    _hs45hb0.detach();
    _hs45hb1.detach();

    _krs.detach();
}

void init() {
    pinMode(BUZZER_PIN, OUTPUT);
    analogWrite(BUZZER_PIN, 0);

    attachServos();
    grabLeft(true, false);
    grabRight(true, false);

    lift(KRS_PARK_US);                    // parked (raised)

}

void grabLeft(bool closed, bool blocking) {
    if (closed) {
        _hs45hb0.writeMicroseconds(HS0_CLOSE_US);
        if (blocking) delay(245);
    } else {
        _hs45hb0.writeMicroseconds(HS0_OPEN_US);
        if (blocking) delay(245);
    }
}

void grabRight(bool closed, bool blocking) {
    if (closed) {
        _hs45hb1.writeMicroseconds(HS1_CLOSE_US);
        if (blocking) delay(245);
    } else {
        _hs45hb1.writeMicroseconds(HS1_OPEN_US);
        if (blocking) delay(245);
    }
}

void grab(bool closed) {
    if (closed) {
        grabLeft(true, false);
        grabRight(true);
    } else {
        grabLeft(false, false);
        grabRight(false);
    }
}





void captureDead()  {
    grabLeft(false);
    lift(KRS_GRAB_US);
    delay(800);
    grabLeft(true);
    lift(KRS_AIR_US);
    delay(400);
}

void captureAlive() {
    grabRight(false);
    lift(KRS_GRAB_US);
    delay(800);
    grabRight(true);
    lift(KRS_AIR_US);
    delay(400);
}

void releaseAll() {
    attachServos();
    grab(false);  delay(300);       // open everything to drop held balls
    detachServos();
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
}

}  // namespace Arm
}  // namespace Actions
