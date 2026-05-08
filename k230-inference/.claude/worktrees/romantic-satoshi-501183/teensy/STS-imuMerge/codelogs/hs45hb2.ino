/*
28 Feb 2026
This code tested the HS45HB servo.
*/

#include <Arduino.h>
#include <Servo.h>

#define _74HTC126EN 2
#define _servo0Pin 3
#define _servo1Pin 4

Servo hs45hb0;
Servo hs45hb1;

void setup() {
  // min/max are pulse widths in microseconds
  // Start conservative: 1000–2000 us
  hs45hb0.attach(_servo0Pin, 1000, 2000);
  hs45hb1.attach(_servo1Pin, 1000, 2000);

//   servo.writeMicroseconds(1500); // center

  // Enable 74HCT126
  pinMode(_74HTC126EN, OUTPUT);
  digitalWrite(_74HTC126EN, HIGH);

  delay(500);
}

void loop() {
    armGripper(true);
    delay(2000);
    
    armGripper(false);
    delay(2000);
}

void armGripper(bool closed){
    if(closed){
        hs45hb0.writeMicroseconds(1000);
        hs45hb1.writeMicroseconds(1000);
    }else{
        hs45hb0.writeMicroseconds(2000);
        hs45hb1.writeMicroseconds(2000);
    }
}
void armLift(bool lift){
    if(lift){
        // KRS3301 control code
    }
    else{
        // KRS3301 control code
    }
}
void dropper(bool drop){
    if(drop){
        // KRS3301 control code
    }
    else{
        // KRS3301 control code
    }
}