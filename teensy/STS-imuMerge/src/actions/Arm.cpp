#include "Arm.h"
#include "../../config.h"
#include "../../pins_teensy.h"
#include "Drive.h"
#include "../processing/K230Decode.h"

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




//

void captureAlive()  {
    Serial.println("[captureAlive] start");

    Processing::K230Decode::tick();
    int16_t centerX = Processing::K230Decode::largestCenterX(K230_CLASS_ALIVE);
    int16_t delta = (centerX >= 0) ? (centerX - (int16_t)K230_FRAME_CENTER_X + 120) : 0;

    // Discretised proportional alignment: the closer to centre, the slower the
    // motors AND the shorter the pulse. Stop, then wait 100ms for the next
    // K230 frame (~10 FPS) before re-checking.
    while (centerX >= 0 && abs(delta) > 25) {
        int absD = abs(delta);
        int speed  = constrain(map(absD, 8, 320, 30, 50), 30, 50);   // px -> motor speed (linear)
        int moveMs = constrain(20 + (int)((130L * absD * absD) / 102400L), 20, 150);  // pulse length: quadratic in delta -> 20 + 180*(absD/320)^2, clamped 10..200
        int dir = (delta < 0) ? -1 : 1;

        Drive::motor(dir * speed, -dir * speed);
        Processing::K230Decode::drainDelay(moveMs);
        Drive::stop();

        Processing::K230Decode::drainDelay(100);   // wait for next frame, keep draining

        centerX = Processing::K230Decode::largestCenterX(K230_CLASS_ALIVE);
        // Tolerate brief detection dropouts (motion blur / ball at frame edge):
        // re-poll a few fresh frames before giving up instead of aborting.
        for (uint8_t r = 0; r < 4 && centerX < 0; r++) {
            Processing::K230Decode::drainDelay(100);
            centerX = Processing::K230Decode::largestCenterX(K230_CLASS_ALIVE);
        }
        if (centerX < 0) {
            break;
        }
        delta = centerX - (int16_t)K230_FRAME_CENTER_X + 120;
    }

    if (centerX < 0) {
        return;
    }

    Drive::stop();

    Serial.println("[captureAlive] grabRight open");
    grabLeft(false);
    Serial.printf("[captureAlive] lift down  us=%d\n", KRS_GRAB_US);

    Drive::motor(-15,-15);
    lift(KRS_GRAB_US);
    Processing::K230Decode::drainDelay(800);
    Drive::motor(35,35);
    Processing::K230Decode::drainDelay(1000);
    grabLeft(true);
    lift(KRS_AIR_US);
    Processing::K230Decode::drainDelay(800);
    Drive::stop();

}

void captureDead() {
    Serial.println("[captureDead] start");

    Processing::K230Decode::tick();
    int16_t centerX = Processing::K230Decode::largestCenterX(K230_CLASS_DEAD);
    int16_t delta = (centerX >= 0) ? (centerX - (int16_t)K230_FRAME_CENTER_X - 120) : 0;

    while (centerX >= 0 && abs(delta) > 25) {
        int absD = abs(delta);
        int speed  = constrain(map(absD, 8, 320, 30, 50), 30, 50);   // px -> motor speed (linear)
        int moveMs = constrain(20 + (int)((130L * absD * absD) / 102400L), 20, 150);  // pulse length: quadratic in delta -> 20 + 180*(absD/320)^2, clamped 10..200
        int dir = (delta < 0) ? -1 : 1;

        Drive::motor(dir * speed, -dir * speed);
        Processing::K230Decode::drainDelay(moveMs);
        Drive::stop();

        Processing::K230Decode::drainDelay(100);   // wait for next frame, keep draining

        centerX = Processing::K230Decode::largestCenterX(K230_CLASS_DEAD);
        // Tolerate brief detection dropouts (motion blur / ball at frame edge):
        // re-poll a few fresh frames before giving up instead of aborting.
        for (uint8_t r = 0; r < 4 && centerX < 0; r++) {
            Processing::K230Decode::drainDelay(100);
            centerX = Processing::K230Decode::largestCenterX(K230_CLASS_DEAD);
        }
        if (centerX < 0) {
            break;
        }
        delta = centerX - (int16_t)K230_FRAME_CENTER_X - 120;
    }

    if (centerX < 0) {
        return;
    }

    Drive::stop();

    Serial.println("[captureDead] grabLeft open");
    grabRight(false);
    Serial.printf("[captureDead] lift down  us=%d\n", KRS_GRAB_US);

    Drive::motor(-15,-15);
    lift(KRS_GRAB_US);
    Processing::K230Decode::drainDelay(600);
    Drive::motor(35,35);
    Processing::K230Decode::drainDelay(700);
    grabRight(true);
    lift(KRS_AIR_US);
    Processing::K230Decode::drainDelay(800);
    Drive::stop();
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
