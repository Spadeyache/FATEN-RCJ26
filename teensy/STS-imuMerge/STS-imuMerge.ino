// =============================================================================
//  STS-imuMerge.ino — Arduino entry point
//
//  This file is intentionally tiny. All logic lives under src/<layer>/.
//  See README.md for the architecture overview and folder rules.
// =============================================================================

#include "config.h"
#include "pins_teensy.h"

#include "src/actions/Drive.h"
#include "src/actions/Forward.h"
#include "src/actions/Turn.h"

#include "src/actions/Arm.h"
#include "src/sensors/IMU.h"
#include "src/sensors/Touch.h"
#include "src/sensors/XIAO_link.h"
#include "src/sensors/K230_link.h"
#include "src/sensors/ToF.h"
#include "src/processing/XiaoDecode.h"
#include "src/processing/K230Decode.h"
#include "src/processing/Mapping.h"
#include "src/state_machine/StateMachine.h"

FLASHMEM void setup() {
    Serial.begin(115200);

    // analogWrite(BUZZER_PIN, 30);
    pinMode(STS_EN_PIN, OUTPUT);
    digitalWrite(STS_EN_PIN, HIGH);
    // pinMode(BUZZER_PIN, OUTPUT);
    // tone(BUZZER_PIN, 2000, 300);

    Actions::Drive::init();
    // Actions::Arm::init();           // servos + KRS, sets initial pose
    Sensors::IMU::init();
    // Sensors::Touch::init();
    Sensors::XIAO_link::init();
    // Sensors::K230_link::init();
    // Sensors::ToF::init();  // Slow VL53L7CX firmware init; defer until mapping/evac needs it.

    StateMachine::init();

    // Avoid blocking startup here; the robot should enter loop() and start driving immediately.
    // Use a non-blocking status indicator if we need boot confirmation later.
}

void loop() {
    // === Pivot-axis bench test ===============================================
    // Hold four fixed wheel gains FOREVER (FL, FR, BL, BR), range ±100.
    // Sweep the numbers one at a time and watch where the robot pivots.
    //   (+,-,+,-) = in-place spin   (+,-,0,0) = front-only   (0,0,+,-) = rear-only
    // Comment this block out to return to normal line following.
    // Actions::Drive::motorRaw(30, -30, 100, -100);
    // Actions::Drive::motorRaw(0, 60, -70, 80);
    // Actions::Drive::motorRaw(-100, 100,0,0);
    // delay(1000);
    // Actions::Drive::motorRaw(40, -40,70,-70);
    // delay(1000);
    // Actions::Drive::vibrateMotor(2, 100.0f, 6, 100);
    // Actions::Drive::motorRaw(100, 100,100,100);
    // Actions::Forward::forward(100, 80);
    // Actions::Turn::turn(90, 40);
    // Actions::Drive::motorRaw(0, 0,0,0);
    // delay(1500);
    // return;
    // ========================================================================

    // 1. Pump sensor I/O (raw bytes in/out).
    Sensors::XIAO_link::tick();
    // Sensors::K230_link::tick();
    Sensors::IMU::tick();
    Sensors::ToF::tick();       // updates global tofFL[8][8]
    // Sensors::ToF::printFL(); // optional debug dump
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


