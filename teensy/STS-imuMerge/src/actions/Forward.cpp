#include "Forward.h"
#include "Drive.h"
#include "../../config.h"
#include "../sensors/IMU.h"
#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>
#include <math.h>

namespace Actions {
namespace Forward {

void forward(float speed, float distance_mm, bool useIMU, bool pumpComms) {
    // Duration scales linearly with speed: calibration is at MAX_MOTOR_SPEED.
    const unsigned long duration =
        (unsigned long)fabsf(distance_mm * FORWARD_MS_PER_MM * MAX_MOTOR_SPEED / speed);

#if PRINT_ACTIONS
    Serial.printf("Forward: %.0f mm @ speed %.0f -> %lu ms\n",
                  distance_mm, speed, duration);
#endif

    const unsigned long start = millis();
    unsigned long lastComms   = 0;

    if (!useIMU) {
        Drive::motor(speed, speed);
        // Always loop (never a blind delay()) so the IMU filter keeps getting
        // fed -- otherwise it goes stale for the whole move and the next
        // tick() after this returns integrates over the entire elapsed gap,
        // producing a bogus attitude jump. pumpComms only gates XIAO/decode.
        while (millis() - start < duration) {
            Sensors::IMU::tick();
            if (pumpComms && millis() - lastComms >= 20) {
                Sensors::XIAO_link::tick();
                Processing::XiaoDecode::tick();
                lastComms = millis();
            }
        }
        Drive::stop();
        return;
    }

    // IMU yaw-hold: correct drift on each tick.
    const float startYaw = (float)Sensors::IMU::getYaw();
    while (millis() - start < duration) {
        Sensors::IMU::tick();
        if (pumpComms && millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick();
            lastComms = millis();
        }

        const float yawError   = (float)Sensors::IMU::getYaw() - startYaw;  // +ve = drifted right
        const float correction = FORWARD_YAW_KP * yawError;

        // Reduce the drifted-to side, boost the other.
        Drive::motor(speed - correction, speed + correction);
    }
    Drive::stop();
}

}  // namespace Forward
}  // namespace Actions
