// // =============================================================================
// //  STS-imuMerge.ino — Arduino entry point
// //
// //  This file is intentionally tiny. All logic lives under src/<layer>/.
// //  See README.md for the architecture overview and folder rules.
// // =============================================================================

// #include "config.h"
// #include "pins_teensy.h"

// #include "src/actions/Drive.h"
// #include "src/actions/Forward.h"
// #include "src/actions/Turn.h"

// #include "src/actions/Arm.h"
// #include "src/sensors/IMU.h"
// #include "src/sensors/Touch.h"
// #include "src/sensors/XIAO_link.h"
// #include "src/sensors/K230_link.h"
// #include "src/processing/XiaoDecode.h"
// #include "src/processing/K230Decode.h"
// #include "src/processing/Mapping.h"
// #include "src/state_machine/StateMachine.h"

// FLASHMEM void setup() {
//     Serial.begin(115200);

//     // analogWrite(BUZZER_PIN, 30);
//     pinMode(STS_EN_PIN, OUTPUT);
//     digitalWrite(STS_EN_PIN, HIGH);
//     // pinMode(BUZZER_PIN, OUTPUT);
//     // tone(BUZZER_PIN, 2000, 300);

//     Actions::Drive::init();
//     // Actions::Arm::init();           // servos + KRS, sets initial pose
//     Sensors::IMU::init();
//     // Sensors::Touch::init();
//     Sensors::XIAO_link::init();
//     // Sensors::K230_link::init();

//     StateMachine::init();

//     // Confirmation beep
//     // delay(50); analogWrite(BUZZER_PIN, 160); delay(40); analogWrite(BUZZER_PIN, 0);
//     delay(50); digitalWrite(BUZZER_PIN, HIGH); delay(400); analogWrite(BUZZER_PIN, 0);
// }

// void loop() {
//     // === Pivot-axis bench test ===============================================
//     // Hold four fixed wheel gains FOREVER (FL, FR, BL, BR), range ±100.
//     // Sweep the numbers one at a time and watch where the robot pivots.
//     //   (+,-,+,-) = in-place spin   (+,-,0,0) = front-only   (0,0,+,-) = rear-only
//     // Comment this block out to return to normal line following.
//     // Actions::Drive::motorRaw(30, -30, 100, -100);
//     // Actions::Drive::motorRaw(0, 60, -70, 80);
//     // Actions::Drive::motorRaw(-100, 100,0,0);
//     // delay(1000);
//     // Actions::Drive::motorRaw(40, -40,70,-70);
//     // delay(1000);
//     // Actions::Drive::vibrateMotor(2, 100.0f, 6, 100);
//     // Actions::Drive::motorRaw(100, 100,100,100);
//     // Actions::Forward::forward(100, 80);
//     // Actions::Turn::turn(90, 40);
//     // Actions::Drive::motorRaw(0, 0,0,0);
//     // delay(1500);
//     // return;
//     // ========================================================================

//     // 1. Pump sensor I/O (raw bytes in/out).
//     Sensors::XIAO_link::tick();
//     // Sensors::K230_link::tick();
//     Sensors::IMU::tick();
//     // Sensors::Touch::tick();

//     // 2. Run processing layer (decode, filter, fuse).
//     Processing::XiaoDecode::tick();
//     // Processing::K230Decode::tick();
//     // Mapping::tick() is called by EVAC_* states only.

//     // 3. Debug Serial commands ('m' = map dump, 'p' = pose print).
//     while (Serial.available()) Processing::Mapping::handleSerial((char)Serial.read());

//     // 4. Run the active state.
//     StateMachine::tick();
// }



// VL53L7CX bare test - STM32duino_VL53L7CX on Wire1.
// One sensor at default address 0x52.
//
// Library: STM32duino VL53L7CX

#include <Wire.h>
#include "src/vendor/STM32duino_VL53L7CX/vl53l7cx_class.h"

VL53L7CX sensor(&Wire1, -1, -1);

static const uint8_t TOF_ADDR = 0x52;
static const uint8_t GRID     = 4;   // 4x4 = 16 zones (use 8 for 8x8)
static const uint8_t TOF_FREQ = 15;  // 8x8 max=15 Hz, 4x4 max=60 Hz
static const uint32_t TOF_I2C_HZ = 50000;  // Extra-stable bring-up speed.

static bool checkStatus(uint8_t status, const char* step) {
    if (status == 0) return true;
    Serial.print("ERROR: ");
    Serial.print(step);
    Serial.print(" failed, status=");
    Serial.println(status);
    return false;
}

static uint8_t scanI2C() {
    uint8_t found = 0;
    Serial.println("I2C scan on Wire1...");
    for (uint8_t addr7 = 1; addr7 < 127; ++addr7) {
        Wire1.beginTransmission(addr7);
        uint8_t err = Wire1.endTransmission();
        if (err == 0) {
            ++found;
            Serial.print("  found 7-bit 0x");
            if (addr7 < 16) Serial.print('0');
            Serial.print(addr7, HEX);
            Serial.print(" / ST 8-bit 0x");
            if ((addr7 << 1) < 16) Serial.print('0');
            Serial.println(addr7 << 1, HEX);
        } else if (err == 4) {
            Serial.print("  unknown error at 7-bit 0x");
            if (addr7 < 16) Serial.print('0');
            Serial.println(addr7, HEX);
        }
    }
    if (found == 0) {
        Serial.println("  no I2C devices found");
    }
    return found;
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {}

    Wire1.begin();
    Wire1.setClock(TOF_I2C_HZ);

    Serial.println("VL53L7CX init on Wire1 using STM32duino...");
    Serial.print("I2C clock Hz: ");
    Serial.println(TOF_I2C_HZ);
    delay(100);
    scanI2C();

    if (!checkStatus(sensor.begin(), "begin")) {
        while (1) {}
    }
    if (!checkStatus(sensor.init_sensor(TOF_ADDR), "init_sensor")) {
        Serial.println("Check SDA/SCL, power, and that no other VL53L7CX is at 0x52.");
        while (1) {}
    }

    const uint8_t res = (GRID == 8) ? VL53L7CX_RESOLUTION_8X8 : VL53L7CX_RESOLUTION_4X4;
    if (!checkStatus(sensor.vl53l7cx_set_resolution(res), "set_resolution")) {
        while (1) {}
    }
    if (!checkStatus(sensor.vl53l7cx_set_ranging_frequency_hz(TOF_FREQ), "set_frequency")) {
        while (1) {}
    }
    if (!checkStatus(sensor.vl53l7cx_start_ranging(), "start_ranging")) {
        while (1) {}
    }

    Serial.println("Ranging. Zone order: row-major, top-left = zone 0.\n");
}

void loop() {
    uint8_t ready = 0;
    if (sensor.vl53l7cx_check_data_ready(&ready) != 0 || ready == 0) return;

    VL53L7CX_ResultsData data;
    if (sensor.vl53l7cx_get_ranging_data(&data) != 0) return;

    for (int row = 0; row < GRID; row++) {
        for (int col = 0; col < GRID; col++) {
            int z = row * GRID + col;
            int target = z * VL53L7CX_NB_TARGET_PER_ZONE;
            int mm = data.distance_mm[target];
            uint8_t st = data.target_status[target];

            if (st == 5 || st == 9) {
                Serial.print(mm);
            } else {
                Serial.print("----");
            }
            Serial.print(col < GRID - 1 ? "\t" : "\n");
        }
    }
    Serial.println("---");
}
