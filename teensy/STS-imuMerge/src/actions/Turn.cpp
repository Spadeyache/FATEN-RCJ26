#include "Turn.h"
#include "Drive.h"
#include "../../config.h"
#include "../../pins_teensy.h"
#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>
#include <math.h>

namespace Actions {
namespace Turn {

void turn(float angle_deg, float speed) {
    // Duration scales the same way as Forward::forward():
    //   calibrated at MAX_MOTOR_SPEED, then inversely scaled by actual speed.
    const unsigned long duration =
        (unsigned long)(fabsf(angle_deg) * TURN_SPIN_MS_PER_DEG * MAX_MOTOR_SPEED / speed);

    // In-place spin: left and right wheels equal and opposite.
    const float l = (angle_deg > 0) ?  speed : -speed;
    const float r = (angle_deg > 0) ? -speed :  speed;
    Drive::motor(l, r);

#if PRINT_ACTIONS
    Serial.printf("Turn: %.1f deg @ speed %.0f -> %lu ms  (L:%.0f R:%.0f)\n",
                  angle_deg, speed, duration, l, r);
#endif

    const unsigned long start = millis();
    unsigned long lastComms   = 0;
    while (millis() - start < duration) {
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick();
            lastComms = millis();
        }
    }

    Drive::stop();
}

}  // namespace Turn
}  // namespace Actions
