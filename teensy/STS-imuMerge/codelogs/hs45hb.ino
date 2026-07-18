/*
28 Feb 2026
This code tested the HS45HB servo.
*/

#include <Arduino.h>
#include <Servo.h>

static const uint8_t SERVO_PIN0 = 3;
static const uint8_t SERVO_PIN1 = 4;

Servo servo0;
Servo servo1;

#define _enPin 2

void setup() {
  // min/max are pulse widths in microseconds
  // Start conservative: 1000–2000 us
  servo0.attach(SERVO_PIN0, 1000, 2000);
  servo1.attach(SERVO_PIN1, 1000, 2000);

//   servo.writeMicroseconds(1500); // center

  // Enable 74HCT126
  pinMode(_enPin, OUTPUT);
  digitalWrite(_enPin, HIGH);

  delay(500);
}

void loop() {
    servo0.writeMicroseconds(1000);
    servo1.writeMicroseconds(1000);
    delay(2000);
    
    servo0.writeMicroseconds(2000);
    servo1.writeMicroseconds(2000);
    delay(2000);
    // Sweep using microseconds (more explicit than degrees)
//   for (int us = 1000; us <= 2000; us += 10) {
//     servo.writeMicroseconds(us);
//     delay(10);
//   }
//   for (int us = 2000; us >= 1000; us -= 10) {
//     servo.writeMicroseconds(us);
//     delay(10);
//   }
}