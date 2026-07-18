// /*
// 3rd Apr 2026
// Merged STS3032, KRS3301, and HS45HB servo code.
// simultanieous operation tested
// */

#include "yacheMPU6050.h"
#include "yacheSTS.h"
#include <IcsHardSerialClass.h>
#include <Servo.h>

#define _HS45HB0PIN 3
#define _HS45HB1PIN 4
#define _KRSID 1



// #define _74HTC126EN 2
const byte _74HTC126EN = 2;
const long KRSBAUDRATE = 115200; //サーボの通信速度
const int KRSTIMEOUT = 400;     //サーボとのシリアル通信に設定する応答待ち時間
IcsHardSerialClass krs(&Serial1,_74HTC126EN,KRSBAUDRATE,KRSTIMEOUT);

Servo _HS45HB0;
Servo _HS45HB1;

yacheSTS _sts;
// yacheMPU6050 _imu;

// --- TIMERS ---
elapsedMillis motorTimer;
IntervalTimer controlTimer;

// --- GLOBAL VARS in (DTCM / RAM1) ---
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
    // a note here if _sts.power takes more than 5ms we are in a infinite loop. Since we run 4 STS3032 at 1Mbps should take arround 100 microsec so we are chill.
}

FLASHMEM void setup() {
  Serial.begin(115200);
  _sts.begin(Serial2);
  //   _imu.begin(Wire, 200.0f);
  //   Serial4.begin(115200); //XIAO
  //   Serial5.begin(115200); //K230
  

  // ---HS45HB servo---
  // Start conservative: 1000–2000 us
  _HS45HB0.attach(_HS45HB0PIN, 1000, 2000);
  _HS45HB1.attach(_HS45HB1PIN, 1000, 2000);

  pinMode(_74HTC126EN, OUTPUT);
  digitalWrite(_74HTC126EN, HIGH);
  delay(200);
  _sts.setWheelMode(true);
  krs.begin();  //サーボモータの通信初期設定
  krs.setSpd(_KRSID, 43); //speed 1-127
  

  // _imu.loadOffsetsFromEEPROM();
  // imu.calibrate(); 

  controlTimer.begin(motorOutput, 5000); // 5ms = 5000 microsecond

  motorTimer = 0;
}

void loop() {
    // do not call _sts.power in loop

    // motor(20,20);
    grabARM(true);
    liftARM(true);
    delay(3000);
    liftARM(false);
    grabARM(false);
    delay(3000);


    

    // if (motorTimer >= 200) {
    //     motor(20,20);
    //     motorTimer -= 200; 
    // }

// best for upward front COM
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

void grabARM(bool closed){
    if(!closed){
        _HS45HB0.writeMicroseconds(1000);
        _HS45HB1.writeMicroseconds(2000);
    }else{
        _HS45HB0.writeMicroseconds(2000);
        _HS45HB1.writeMicroseconds(1000);
    }
}
void liftARM(bool lift){
    if(lift){
        krs.setPos(_KRSID,11500);  //setPose range: 3500〜11500
    }
    else{
      krs.setPos(_KRSID,4400);  //位置指令　ID:0サーボを7500へ 中央
    }
    delay(800);
    krs.setFree(_KRSID); 
}

// make a struct and when arm is droped withingh any command arm will drop and open the hs45hb if its not holding a ball