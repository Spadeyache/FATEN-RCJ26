#include "WallFollow.h"
#include "Drive.h"
#include "Forward.h"
#include "Turn.h"
#include "../sensors/ToF.h"
#include "../sensors/Touch.h"

#include <Arduino.h>

namespace Actions {
namespace WallFollow {

namespace {
    // --- Front-bumper obstacle recovery ---------------------------------------
    constexpr float OBSTACLE_BACKUP_MM    = 80.0f;   // TODO: tune on the bench
    constexpr float OBSTACLE_BACKUP_SPEED = 40.0f;
    constexpr float OBSTACLE_TURN_DEG     = -90.0f;  // negative = left (Turn::turn() convention)
    constexpr float OBSTACLE_TURN_SPEED   = 50.0f;

    void handleObstacle() {
        Drive::stop();
        Forward::forward(-OBSTACLE_BACKUP_SPEED, OBSTACLE_BACKUP_MM);
        Turn::turn(OBSTACLE_TURN_DEG, OBSTACLE_TURN_SPEED);
    }

    // --- Wall-follow PID tuning -----------------------------------------------
    //   error = measured distance - targetMm
    //     error > 0 -> too far from the wall -> steer right (toward the wall)
    //     error < 0 -> too close to the wall -> steer left  (away from the wall)
    //   Not hardware-validated yet — flip the sign on `correction` below if the
    //   robot steers the wrong way on the bench.
    constexpr float PID_KP = 0.3f;   // TODO: tune on the bench
    constexpr float PID_KI = 0.0f;
    constexpr float PID_KD = 0.0f;

    constexpr float INTEGRAL_LIMIT = 200.0f;

    // Center 2x2 block of the 8x8 grid: the zones looking straight out to the
    // side (perpendicular to travel), least sensitive to wall-end/corner noise
    // at the FOV edges.
    constexpr uint8_t CENTER_LO = 3;
    constexpr uint8_t CENTER_HI = 4;

    // Averages the valid (non -1) cells in the center block.
    // Returns false if none of them are valid (no wall in range this tick).
    bool centerDistanceMm(float& out) {
        int32_t sum   = 0;
        uint8_t count = 0;
        for (uint8_t row = CENTER_LO; row <= CENTER_HI; ++row) {
            for (uint8_t col = CENTER_LO; col <= CENTER_HI; ++col) {
                const int16_t mm = tofFL[row][col];
                if (mm < 0) continue;
                sum += mm;
                ++count;
            }
        }
        if (count == 0) return false;
        out = (float)sum / (float)count;
        return true;
    }
}  // namespace

bool tick(float targetMm, float baseSpeed) {
    static float         integral  = 0.0f;
    static float         lastError = 0.0f;
    static unsigned long lastTime  = 0;

    if (Sensors::Touch::front()) {
        handleObstacle();
        integral  = 0.0f;
        lastError = 0.0f;
        lastTime  = micros();   // avoid a dt spike from the blocking maneuver
        return false;
    }

    float distanceMm;
    if (!centerDistanceMm(distanceMm)) {
        integral = 0.0f;   // don't let error wind up while the wall is out of range
        Drive::stop();
        return false;
    }

    const unsigned long now = micros();
    float dt = (now - lastTime) * 1e-6f;
    lastTime = now;
    if (dt <= 0.0f || dt > 0.5f) dt = 0.05f;   // first call / stall guard (~TOF_FREQ_HZ)

    const float error      = distanceMm - targetMm;
    const float derivative = (error - lastError) / dt;
    lastError = error;

    integral += error * dt;
    integral  = constrain(integral, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);

    const float correction = PID_KP * error + PID_KI * integral + PID_KD * derivative;

    Drive::motor(baseSpeed + correction, baseSpeed - correction);
    return true;
}

}  // namespace WallFollow
}  // namespace Actions
