#include "Turn.h"
#include "Drive.h"
#include "../../config.h"
#include "../../pins_teensy.h"
#include "../sensors/XIAO_link.h"
#include "../sensors/IMU.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>
#include <math.h>

namespace Actions {
namespace Turn {

namespace {

void turnImpl(float angle_deg, float speed, bool useGravityProfile) {
    // Duration scales the same way as Forward::forward():
    //   calibrated at MAX_MOTOR_SPEED, then inversely scaled by actual speed.
    const unsigned long duration =
        (unsigned long)(fabsf(angle_deg) * TURN_SPIN_MS_PER_DEG * MAX_MOTOR_SPEED / speed);

    // In-place spin: left and right wheels equal and opposite.
    const float l = (angle_deg > 0) ?  speed : -speed;
    const float r = (angle_deg > 0) ? -speed :  speed;
    const float turnNorm = (angle_deg > 0) ? 1.0f : -1.0f;
    if (useGravityProfile) Drive::motorTurnGravityProfiled(l, r, turnNorm);
    else                   Drive::motor(l, r);

#if PRINT_ACTIONS
    Serial.printf("Turn%s: %.1f deg @ speed %.0f -> %lu ms  (L:%.0f R:%.0f)\n",
                  useGravityProfile ? "" : "Raw", angle_deg, speed, duration, l, r);
#endif

    const unsigned long start = millis();
    unsigned long lastComms   = 0;
    while (millis() - start < duration) {
        // IMU sampling is ISR-driven now (fixed-rate IntervalTimer), so the spin
        // no longer starves the filter -- nothing to pump here. Just keep comms +
        // the gravity-profiled motor command refreshed.
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick();
            if (useGravityProfile) Drive::motorTurnGravityProfiled(l, r, turnNorm);
            else                   Drive::motor(l, r);
            lastComms = millis();
        }
    }

    Drive::stop();
}

}  // namespace

void turn(float angle_deg, float speed) {
    turnImpl(angle_deg, speed, true);
}

void turnRaw(float angle_deg, float speed) {
    turnImpl(angle_deg, speed, false);
}

}  // namespace Turn
}  // namespace Actions
