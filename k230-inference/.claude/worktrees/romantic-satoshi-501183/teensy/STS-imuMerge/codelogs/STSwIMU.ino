/*
25 Feb 2026
This code tested the two optimized "yacheSTS.h" "yacheMPU6050.h" library in a single code.
*/

#include "yacheMPU6050.h"
#include "yacheSTS.h"

#define _74HTC126EN 2

yacheMPU6050 _imu;
yacheSTS _sts;

elapsedMillis imuTimer;

void setup() {
    Serial.begin(115200);
    _sts.begin(Serial1); 
    _imu.begin(Wire, 200.0f);

    // Enable 74HCT126
    pinMode(_74HTC126EN, OUTPUT);
    digitalWrite(_74HTC126EN, HIGH);

    delay(500); // Allow servos to power up

    _sts.setWheelMode(true);
    _imu.loadOffsetsFromEEPROM();
    
    Serial.println("System Ready.");
    imuTimer = 0;
}

void loop() {
    // 200Hz Control Loop
    if (imuTimer >= 5) {
        // Now using optimized float inputs
        _sts.power(-20.0f, 20.0f, -20.0f, 20.0f); 
        _imu.update(); 
        imuTimer -= 5; 
    }

    _imu.printQuat();
    
}