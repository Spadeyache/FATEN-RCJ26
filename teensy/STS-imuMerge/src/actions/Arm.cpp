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
    constexpr int KRS_GRAB_US    = 2040;
    constexpr int KRS_AIR_US    = 1450;

    constexpr int HS0_CLOSE_US = 2000;
    constexpr int HS0_OPEN_US  = 1000;
    constexpr int HS1_CLOSE_US = 1000;
    constexpr int HS1_OPEN_US  = 2000;

    constexpr int GRAB_RAMP_STEP_US = 10;
    constexpr int GRAB_RAMP_DELAY_MS = 5;

    int _hs0CurrentUs = HS0_CLOSE_US;
    int _hs1CurrentUs = HS1_CLOSE_US;

    void writeServoSmooth(Servo& servo, int& currentUs, int targetUs, bool blocking) {
        if (!blocking || currentUs == targetUs) {
            servo.writeMicroseconds(targetUs);
            currentUs = targetUs;
            return;
        }

        int step = (targetUs > currentUs) ? GRAB_RAMP_STEP_US : -GRAB_RAMP_STEP_US;
        for (int us = currentUs; (step > 0) ? (us < targetUs) : (us > targetUs); us += step) {
            servo.writeMicroseconds(us);
            delay(GRAB_RAMP_DELAY_MS);
        }

        servo.writeMicroseconds(targetUs);
        currentUs = targetUs;
    }
}

void attachServos() {
    _hs45hb0.attach(HS45HB0_PIN, 1000, 2000);
    _hs45hb1.attach(HS45HB1_PIN, 1000, 2000);
    
    _krs.attach(KRS_PWM_PIN, 600, 2400);
}

void detachServos() {
    _hs45hb0.detach();
    _hs45hb1.detach();

    // _krs.detach();
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
    int targetUs = closed ? HS1_CLOSE_US : HS1_OPEN_US;
    writeServoSmooth(_hs45hb1, _hs0CurrentUs, targetUs, blocking);
}

void grabRight(bool closed, bool blocking) {
    int targetUs = closed ? HS0_CLOSE_US : HS0_OPEN_US;
    writeServoSmooth(_hs45hb0, _hs1CurrentUs, targetUs, blocking);
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

void releaseAll() {
    attachServos();
    grab(false);  delay(300);       // open everything to drop held balls
    detachServos();
}

// Move the lift to a raw PWM pulse width (microseconds) and HOLD it there.
// PWM mode has no "free" — the Servo library keeps refreshing the pulse.
void lift(int us) {
    us = constrain(us, 600, 2400);
    _krs.writeMicroseconds(us);
#if PRINT_ACTIONS
    Serial.print("KRS PWM lift -> "); Serial.println(us);
#endif
}

// Named lift poses (the only lift values callers need to know about).
void liftDown()  { lift(KRS_GRAB_US); }
void liftCarry() { lift(KRS_AIR_US);  }
void liftPark()  { lift(KRS_PARK_US); }

}  // namespace Arm
}  // namespace Actions
