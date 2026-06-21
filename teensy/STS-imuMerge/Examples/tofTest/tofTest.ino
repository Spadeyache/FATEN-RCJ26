
// VL53L7CX bare test - STM32duino_VL53L7CX on Wire1.
// One sensor at default address 0x52.
//
// Library: STM32duino VL53L7CX

#include "src/sensors/ToF.h"

void setup() {
    Serial.begin(115200);
    while (!Serial) {}

    Sensors::ToF::init();
    Serial.println("Call Sensors::ToF::tick() to update global tofFL[8][8].");
}

void loop() {
    if (Sensors::ToF::tick()) { // overwrites global tofFL[8][8] when a new frame is ready
        Sensors::ToF::printFL(); // debug print showing how tofFL is being read
    }
}
