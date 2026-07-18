#include "yacheSTS.h"


yacheSTS _sts;

void setup() {
    _sts.begin(Serial1, 2);    // Start communication

    delay(500);

    _sts.setWheelMode(true);   // Set the "Wheel" personality
}

void loop() {
    // Drive forward at 50% power
    _sts.power(-50, -50, -50, -50); 
    delay(200);
    _sts.stop();
    delay(200);
}