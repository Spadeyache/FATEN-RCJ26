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

    pinMode(STS_EN_PIN, OUTPUT);
    digitalWrite(STS_EN_PIN, HIGH);
    pinMode(BUZZER_PIN, OUTPUT);
    tone(BUZZER_PIN, 4000, 300);

    Actions::Drive::init();
    Actions::Arm::init();           // servos + KRS, sets initial pose
    // Sensors::IMU::init();
    // Sensors::Touch::init();
    // Sensors::XIAO_link::init();
    // Sensors::K230_link::init();
    // Sensors::ToF::init();

    // StateMachine::init();
}

void loop() {
    // === KRS lift bench test =================================================
    Actions::Arm::lift(5500);   // low
    delay(200);
    Actions::Arm::lift(9500);   // high
    delay(200);
    return;
    // =========================================================================

    // 1. Pump sensor I/O (raw bytes in/out).
    // Sensors::XIAO_link::tick();
    // Sensors::K230_link::tick();
    // Sensors::IMU::tick();
    // Sensors::ToF::tick();
    // Sensors::Touch::tick();

    // 2. Run processing layer (decode, filter, fuse).
    // Processing::XiaoDecode::tick();
    // Processing::K230Decode::tick();

    // 3. Debug Serial commands ('m' = map dump, 'p' = pose print).
    // while (Serial.available()) Processing::Mapping::handleSerial((char)Serial.read());

    // 4. Run the active state.
    // StateMachine::tick();
}
