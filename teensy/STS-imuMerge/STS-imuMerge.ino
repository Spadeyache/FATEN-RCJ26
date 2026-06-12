// /*
// 26 Feb 2026
// This code tested 
// */
// #include "pins_teensy.h"
// #include "src/drivers/yacheMPU6050.h"
// #include "src/drivers/yacheSTS.h"

// // yacheMPU6050 _imu(Wire1);
// yacheSTS _sts;

// elapsedMillis motorTimer;

// IntervalTimer controlTimer;

// // GLOBAL VARS in (DTCM / RAM1)
// volatile float32_t frontLeftGain = 0.0f;
// volatile float32_t frontRightGain = 0.0f;
// volatile float32_t backLeftGain = 0.0f;
// volatile float32_t backRightGain = 0.0f;
// // volatile float32_t pitch = 0;
// // volatile float32_t roll = 0;


// // A funciton that has priority in precicly updating the imu & motorGains
// FASTRUN void motorOutput(){
//     // _imu.update();
//     // pitch = _imu.getPitch();
//     // roll = _imu.getRoll();

//     // ADD LOGIC to encoporate the pitch and roll.
//     _sts.power(frontLeftGain, frontRightGain, backLeftGain, backRightGain);
//     // a note here if .power takes more than 5ms we are in a infinite loop. Since we run 4 STS3032 at 1Mbps should take arround 100 microsec so we are chill.
// }

// FLASHMEM void setup() {
//     Serial.begin(115200);
//     _sts.begin(STS_SERIAL);
//     // _imu.begin(Wire, 200.0f);

//     pinMode(STS_EN_PIN, OUTPUT);
//     digitalWrite(STS_EN_PIN, HIGH);

//     delay(500);

//     _sts.setWheelMode(true);

//     // _imu.loadOffsetsFromEEPROM();
//     // imu.calibrate(); 
    
//     Serial.println("System Ready.");
//     // imuTimer = 0;

    
//     controlTimer.begin(motorOutput, 5000); // 5ms = 5000 microseconds
// }

// void loop() {
//     // do not call _sts.power in loop

//     motor(100,100);

// // besto for upward front COM
//     // _sts.power(30.0f, -15.0f, 55.0f, -25.0f);
//     // delay(3000);
//     // _sts.power(-15.0f, 30.0f, -25.0f, 55.0f);
//     // delay(3000);


//     // _imu.printQuat(); //prints every 250ms
    
// }



// FASTRUN void motor(float32_t left, float32_t right){
//     noInterrupts(); // Safety: update all 4 at once
//     frontLeftGain = left; frontRightGain = right;
//     backLeftGain = left;  backRightGain = right;
//     interrupts();
// }


// =============================================================================
//  STS-imuMerge.ino — Arduino entry point
//
//  This file is intentionally tiny. All logic lives under src/<layer>/.
//  See README.md for the architecture overview and folder rules.
// =============================================================================

#include "config.h"
#include "pins_teensy.h"

#include "src/actions/Drive.h"
#include "src/actions/Arm.h"
#include "src/sensors/IMU.h"
#include "src/sensors/Touch.h"
#include "src/sensors/XIAO_link.h"
#include "src/sensors/K230_link.h"
#include "src/processing/XiaoDecode.h"
#include "src/processing/K230Decode.h"
#include "src/processing/Mapping.h"
#include "src/state_machine/StateMachine.h"

FLASHMEM void setup() {
    Serial.begin(115200);

    // analogWrite(BUZZER_PIN, 30);
    pinMode(STS_EN_PIN, OUTPUT);
    digitalWrite(STS_EN_PIN, HIGH);

    Actions::Drive::init();
    // Actions::Arm::init();           // servos + KRS, sets initial pose
    // Sensors::IMU::init();
    // Sensors::Touch::init();
    // Sensors::XIAO_link::init();
    // Sensors::K230_link::init();

    // StateMachine::init();

    // Confirmation beep
    // delay(50); analogWrite(BUZZER_PIN, 160); delay(40); analogWrite(BUZZER_PIN, 0);
}

void loop() {
    // === Pivot-axis bench test ===============================================
    // Hold four fixed wheel gains FOREVER (FL, FR, BL, BR), range ±100.
    // Sweep the numbers one at a time and watch where the robot pivots.
    //   (+,-,+,-) = in-place spin   (+,-,0,0) = front-only   (0,0,+,-) = rear-only
    // Comment this block out to return to normal line following.
    // Actions::Drive::motorRaw(30, -30, 100, -100);
    // Actions::Drive::motorRaw(0, 60, -70, 80);
    // Actions::Drive::motorRaw(0, 100,0,100);
    // Actions::Drive::vibrateMotor(2, 100.0f, 6, 100);
    // Actions::Drive::motorRaw(100, 100,100,100);
    // return;
    // ========================================================================

    // 1. Pump sensor I/O (raw bytes in/out).
    Sensors::XIAO_link::tick();
    // Sensors::K230_link::tick();
    // Sensors::IMU::tick();
    // Sensors::Touch::tick();

    // 2. Run processing layer (decode, filter, fuse).
    Processing::XiaoDecode::tick();
    // Processing::K230Decode::tick();
    // Mapping::tick() is called by EVAC_* states only.

    // 3. Debug Serial commands ('m' = map dump, 'p' = pose print).
    while (Serial.available()) Processing::Mapping::handleSerial((char)Serial.read());

    // 4. Run the active state.
    StateMachine::tick();
}
