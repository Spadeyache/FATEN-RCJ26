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
    constexpr bool DEBUG_WALL_FOLLOW = false;

    // Obstacle recovery when the front touch sensor fires.
    constexpr float OBSTACLE_BACKUP_MM    = 48.0f;
    constexpr float OBSTACLE_BACKUP_SPEED = 40.0f;
    constexpr float OBSTACLE_TURN_DEG     = -45.0f;//90
    constexpr float OBSTACLE_TURN_SPEED   = 60.0f;

    // PID: error = measured wall distance - target distance.
    constexpr float PID_KP = 3.0f;
    constexpr float PID_KI = 0.0f;
    constexpr float PID_KD = 1.57f;
    constexpr float INTEGRAL_LIMIT = 200.0f;

    // 2x2 ToF block used as the right-wall distance estimate.
    constexpr uint8_t ZONE_ROW_LO = 3;
    constexpr uint8_t ZONE_ROW_HI = 4;
    constexpr uint8_t ZONE_COL_LO = 2;
    constexpr uint8_t ZONE_COL_HI = 3;

    // Exit candidate signal once a wall has been acquired.
    constexpr float   EXIT_FAR_MM         = 200.0f;
    constexpr uint8_t EXIT_FAR_FRAMES     = 1;
    constexpr uint8_t EXIT_INVALID_FRAMES = 1;
    constexpr uint8_t WALL_ACQUIRE_FRAMES = 5;

    float         s_integral = 0.0f;
    float         s_lastError = 0.0f;
    unsigned long s_lastTime = 0;
    uint8_t       s_walledFrames = 0;
    uint8_t       s_farFrames = 0;
    uint8_t       s_invalidFrames = 0;

    void handleObstacle() {
        Drive::stop();
        Forward::forward(-OBSTACLE_BACKUP_SPEED, OBSTACLE_BACKUP_MM);
        Turn::turn(OBSTACLE_TURN_DEG, OBSTACLE_TURN_SPEED);
    }

    bool readWallDistanceMm(float& out) {
        int32_t sum = 0;
        uint8_t count = 0;

        for (uint8_t row = ZONE_ROW_LO; row <= ZONE_ROW_HI; ++row) {
            for (uint8_t col = ZONE_COL_LO; col <= ZONE_COL_HI; ++col) {
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

    void resetExitCounters() {
        s_farFrames = 0;
        s_invalidFrames = 0;
    }

    Status classifyNoWall(bool valid, float distanceMm) {
        s_integral = 0.0f;

        if (valid) {
            ++s_farFrames;
            s_invalidFrames = 0;
        } else {
            ++s_invalidFrames;
            s_farFrames = 0;
        }

        const bool armed = s_walledFrames >= WALL_ACQUIRE_FRAMES;
        const bool candidate =
            s_farFrames >= EXIT_FAR_FRAMES ||
            s_invalidFrames >= EXIT_INVALID_FRAMES;

        const Status status = (armed && candidate) ? Status::EXIT_CANDIDATE
                                                   : Status::NO_WALL;
        if (DEBUG_WALL_FOLLOW) {
            Serial.printf("[WF] no-wall valid=%d dist=%.0f wall=%u far=%u inv=%u st=%d\n",
                          valid ? 1 : 0, distanceMm,
                          s_walledFrames, s_farFrames, s_invalidFrames,
                          (int)status);
        }
        return status;
    }

    void updateWallAcquired() {
        if (s_walledFrames < WALL_ACQUIRE_FRAMES) ++s_walledFrames;
        resetExitCounters();
    }

    float pidCorrection(float distanceMm, float targetMm) {
        const unsigned long now = micros();
        float dt = (now - s_lastTime) * 1e-6f;
        s_lastTime = now;
        if (dt <= 0.0f || dt > 0.5f) dt = 0.05f;

        const float error = distanceMm - targetMm;
        const float derivative = (error - s_lastError) / dt;
        s_lastError = error;

        s_integral += error * dt;
        s_integral = constrain(s_integral, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);

        return PID_KP * error + PID_KI * s_integral + PID_KD * derivative;
    }
}  // namespace

void reset() {
    s_integral = 0.0f;
    s_lastError = 0.0f;
    s_lastTime = micros();
    s_walledFrames = 0;
    resetExitCounters();
}

Status tick(float targetMm, float baseSpeed) {
    if (Sensors::Touch::front()) {
        if (DEBUG_WALL_FOLLOW) Serial.println("[WF] touch recovery");
        handleObstacle();
        reset();
        return Status::NO_WALL;
    }

    float distanceMm = 0.0f;
    const bool valid = readWallDistanceMm(distanceMm);
    const bool noWall = !valid || distanceMm >= EXIT_FAR_MM;

    if (noWall) {
        const Status status = classifyNoWall(valid, distanceMm);
        if (status == Status::EXIT_CANDIDATE) {
            Drive::stop();
        } else {
            Drive::motor(baseSpeed, baseSpeed);
        }
        return status;
    }

    updateWallAcquired();

    const float correction = pidCorrection(distanceMm, targetMm);
    Drive::motor(baseSpeed + correction, baseSpeed - correction);
    return Status::FOLLOWING;
}

}  // namespace WallFollow
}  // namespace Actions
