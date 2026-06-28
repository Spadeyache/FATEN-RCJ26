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

// Align `alignCls` to the given arm's offset, then grab with that arm. Blocking.
// Offset & grab timing are per physical side; the ball type only drives which
// detections we align to, so either arm can grab either type (overflow case).
//   LEFT  arm: offset +120, grab timing 800/1000
//   RIGHT arm: offset -120, grab timing 600/700
void grabArm(Side side, uint8_t alignCls) {
    const int offset = (side == LEFT) ? +120 : -120;
    Serial.printf("[grabArm] side=%s cls=%u\n", side == LEFT ? "LEFT" : "RIGHT", alignCls);

    Processing::K230Decode::tick();
    int16_t centerX = Processing::K230Decode::largestCenterX(alignCls);
    int16_t delta = (centerX >= 0) ? (centerX - (int16_t)K230_FRAME_CENTER_X + offset) : 0;

    // Discretised proportional alignment, tolerant of brief detection dropouts.
    while (centerX >= 0 && abs(delta) > 25) {
        int absD = abs(delta);
        int speed  = constrain(map(absD, 8, 320, 30, 50), 30, 50);
        int moveMs = constrain(20 + (int)((130L * absD * absD) / 102400L), 20, 150);
        int dir = (delta < 0) ? -1 : 1;

        Drive::motor(dir * speed, -dir * speed);
        Processing::K230Decode::drainDelay(moveMs);
        Drive::stop();
        Processing::K230Decode::drainDelay(100);

        centerX = Processing::K230Decode::largestCenterX(alignCls);
        for (uint8_t r = 0; r < 4 && centerX < 0; r++) {
            Processing::K230Decode::drainDelay(100);
            centerX = Processing::K230Decode::largestCenterX(alignCls);
        }
        if (centerX < 0) break;
        delta = centerX - (int16_t)K230_FRAME_CENTER_X + offset;
    }
    if (centerX < 0) return;   // lost during align; the caller's confirm won't count it

    Drive::stop();

    const int tDown = (side == LEFT) ? 800  : 600;
    const int tFwd  = (side == LEFT) ? 1000 : 700;

    if (side == LEFT) grabLeft(false); else grabRight(false);
    Drive::motor(-15,-15);
    lift(KRS_GRAB_US);
    Processing::K230Decode::drainDelay(tDown);
    Drive::motor(35,35);
    Processing::K230Decode::drainDelay(tFwd);
    if (side == LEFT) grabLeft(true); else grabRight(true);
    lift(KRS_AIR_US);
    Processing::K230Decode::drainDelay(800);
    Drive::stop();
}

// Natural-arm convenience wrappers (silver/alive -> LEFT, dead/black -> RIGHT).
void captureAlive() { grabArm(LEFT,  K230_CLASS_ALIVE); }
void captureDead()  { grabArm(RIGHT, K230_CLASS_DEAD);  }

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
