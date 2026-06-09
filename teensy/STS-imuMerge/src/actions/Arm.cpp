#include "Arm.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include <Arduino.h>
#include <Servo.h>
#include "IcsHardSerialClass.h"

namespace Actions {
namespace Arm {

namespace {
    Servo              _grabServo0;
    Servo              _grabServo1;
    IcsHardSerialClass _krs(&Serial1, PIN_74HCT126_EN, KRS_BAUD, KRS_TIMEOUT);
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
    grab(false);                          // open gripper on boot

    _krs.begin();
    delay(200);                           // let the KRS bus / servo settle
    _krs.setSpd(KRS_ID, KRS_SPD);
    lift(11050);                          // parked

    detachGrabServos();                   // silence the hobby servos
}

void grab(bool closed) {
    if (closed) {
        _grabServo0.writeMicroseconds(1700);
        _grabServo1.writeMicroseconds(1300);
    } else {
        _grabServo0.writeMicroseconds(1000);
        _grabServo1.writeMicroseconds(2000);
    }
}

// Blocking. Only call from setup() or controlled-stop sequences.
void lift(int pos) {
    int rd = _krs.setPos(KRS_ID, pos);
#if PRINT_ACTIONS
    Serial.print("KRS setPos("); Serial.print(pos);
    Serial.print(") -> readback="); Serial.println(rd);   // -1 == no reply on bus
#endif
    delay(800);
    _krs.setFree(KRS_ID);
}

}  // namespace Arm
}  // namespace Actions
