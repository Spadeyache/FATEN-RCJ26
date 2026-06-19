#include "Touch.h"
#include "../../config.h"
#include "../../pins_teensy.h"
#include <Arduino.h>

namespace Sensors {
namespace Touch {

namespace {
    bool _front    = false;
    bool _conduct0 = false;
    bool _conduct1 = false;
}

void init() {
    pinMode(TOUCH_FRONT_PIN, INPUT_PULLUP);
    pinMode(CONDUCT0_PIN,    INPUT_PULLUP);
    pinMode(CONDUCT1_PIN,    INPUT_PULLUP);
    Serial.println("Touch ready.");
}

void tick() {
    // Front bumper + conduct1 are active-low; conduct0 is reported as-read
    // (matches the original Sensors.ino convention â€” preserved verbatim).
    _front    = !digitalRead(TOUCH_FRONT_PIN);
    _conduct0 =  digitalRead(CONDUCT0_PIN);
    _conduct1 = !digitalRead(CONDUCT1_PIN);
}

bool front()    { return _front; }
bool conduct0() { return _conduct0; }
bool conduct1() { return _conduct1; }

}  // namespace Touch
}  // namespace Sensors
