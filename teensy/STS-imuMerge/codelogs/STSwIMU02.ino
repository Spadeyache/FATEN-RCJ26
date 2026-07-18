/*
26 Feb 2026
This code tested 
*/
#include "yacheMPU6050.h"
#include "yacheSTS.h"

#define _74HTC126EN 2

yacheMPU6050 _imu;
yacheSTS _sts;

elapsedMillis motorTimer;

IntervalTimer controlTimer;

// GLOBAL VARS in (DTCM / RAM1)
volatile float32_t frontLeftGain = 0.0f;
volatile float32_t frontRightGain = 0.0f;
volatile float32_t backLeftGain = 0.0f;
volatile float32_t backRightGain = 0.0f;
volatile float32_t pitch = 0;
volatile float32_t roll = 0;


// A funciton that has priority in precicly updating the imu & motorGains
FASTRUN void motorOutput(){
    // _imu.update();
    // pitch = _imu.getPitch();
    // roll = _imu.getRoll();

    // ADD LOGIC to encoporate the pitch and roll.
    _sts.power(frontLeftGain, frontRightGain, backLeftGain, backRightGain);
    // a note here if .power takes more than 5ms we are in a infinite loop. Since we run 4 STS3032 at 1Mbps should take arround 100 microsec so we are chill.
}

FLASHMEM void setup() {
    Serial.begin(115200);
    _sts.begin(Serial1);
    // _imu.begin(Wire, 200.0f);

    pinMode(_74HTC126EN, OUTPUT);
    digitalWrite(_74HTC126EN, HIGH);

    delay(500);

    _sts.setWheelMode(true);

    // _imu.loadOffsetsFromEEPROM();
    // imu.calibrate(); 
    
    Serial.println("System Ready.");
    // imuTimer = 0;

    
    controlTimer.begin(motorOutput, 5000); // 5ms = 5000 microseconds
}

void loop() {
    // do not call _sts.power in loop

    
    if (motorTimer >= 200) {
        motor(20,20);
        motorTimer -= 200; 
    }

// besto for upward front COM
    // _sts.power(30.0f, -15.0f, 55.0f, -25.0f);
    // delay(3000);
    // _sts.power(-15.0f, 30.0f, -25.0f, 55.0f);
    // delay(3000);


    // _imu.printQuat(); //prints every 250ms
    
}



FASTRUN void motor(float32_t left, float32_t right){
    noInterrupts(); // Safety: update all 4 at once
    frontLeftGain = left; frontRightGain = right;
    backLeftGain = left;  backRightGain = right;
    interrupts();
}