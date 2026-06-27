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
    constexpr int KRS_GRAB_US    = 2100;
    constexpr int KRS_AIR_US    = 1450;

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




//

void captureAlive()  {
    Serial.println("[captureAlive] start");

    Processing::K230Decode::tick();
    int16_t centerX = Processing::K230Decode::largestCenterX(K230_CLASS_ALIVE);
    int16_t delta = (centerX >= 0) ? (centerX - (int16_t)K230_FRAME_CENTER_X - 100) : 0;

    Serial.printf("[captureAlive] initial centerX=%d  delta=%d\n", centerX, delta);

    // Discretised proportional alignment: the closer to centre, the slower the
    // motors AND the shorter the pulse. Stop, then wait 100ms for the next
    // K230 frame (~10 FPS) before re-checking.
    while (centerX >= 0 && abs(delta) > 8) {
        int absD = abs(delta);
        int speed  = constrain(map(absD, 8, 320, 30, 50), 30, 50);   // px -> motor speed (linear)
        int moveMs = constrain(20 + (int)((130L * absD * absD) / 102400L), 20, 150);  // pulse length: quadratic in delta -> 20 + 180*(absD/320)^2, clamped 10..200
        int dir = (delta < 0) ? -1 : 1;

        Drive::motor(dir * speed, -dir * speed);
        delay(moveMs);
        Drive::stop();

        delay(100);   // wait for next K230 frame (~10 FPS)

        Processing::K230Decode::tick();
        centerX = Processing::K230Decode::largestCenterX(K230_CLASS_ALIVE);
        if (centerX < 0) {
            break;
        }
        delta = centerX - (int16_t)K230_FRAME_CENTER_X - 100;
    }

    if (centerX < 0) {
        return;
    }

    Drive::stop();

    Serial.println("[captureAlive] grabLeft open");
    grabLeft(false);
    Serial.printf("[captureAlive] lift down  us=%d\n", KRS_GRAB_US);
    lift(KRS_GRAB_US);
    delay(1000);
    Serial.println("[captureAlive] grabLeft close");
    grabLeft(true);
    Serial.printf("[captureAlive] lift up  us=%d\n", KRS_AIR_US);
    lift(KRS_AIR_US);
    delay(1000);

    Serial.println("[captureAlive] done");
}

void captureDead() {
    Serial.println("[captureAlive] start");

    Processing::K230Decode::tick();
    int16_t centerX = Processing::K230Decode::largestCenterX(K230_CLASS_ALIVE);
    int16_t delta = (centerX >= 0) ? (centerX - (int16_t)K230_FRAME_CENTER_X + 100) : 0;

    Serial.printf("[captureAlive] initial centerX=%d  delta=%d\n", centerX, delta);

    while (centerX >= 0 && abs(delta) > 8) {
        int absD = abs(delta);
        int speed  = constrain(map(absD, 8, 320, 30, 65), 30, 65);   // px -> motor speed (linear)
        int moveMs = constrain(20 + (int)((120L * absD * absD) / 102400L), 20, 140);  // pulse length: quadratic in delta -> 20 + 180*(absD/320)^2, clamped 10..200
        int dir = (delta < 0) ? -1 : 1;

        Drive::motor(dir * speed, -dir * speed);
        delay(moveMs);
        Drive::stop();

        delay(100);   // wait for next K230 frame (~10 FPS)

        Processing::K230Decode::tick();
        centerX = Processing::K230Decode::largestCenterX(K230_CLASS_ALIVE);
        if (centerX < 0) {
            break;
        }
        delta = centerX - (int16_t)K230_FRAME_CENTER_X + 100;
    }

    if (centerX < 0) {
        return;
    }
    grabRight(false);
    lift(KRS_GRAB_US);
    delay(1400);
    grabRight(true);
    lift(KRS_AIR_US);
    delay(1400);
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
