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

    // Obstacle recovery when the front touch sensor fires. The turn angle
    // depends on what we were doing when it fired: a wide turn while an
    // established wall/pause was active, a narrower one while still blind
    // -searching for the wall (see obstacleTurnDeg() / s_lastStatus below).
    constexpr float OBSTACLE_BACKUP_MM    = 48.0f;
    constexpr float OBSTACLE_BACKUP_SPEED = 40.0f;
    constexpr float OBSTACLE_TURN_DEG     = -90.0f;
    constexpr float SEARCH_TURN_DEG       = -50.0f;
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
    constexpr float   EXIT_FAR_MM         = 230.0f;
    constexpr uint8_t EXIT_FAR_FRAMES     = 1;
    constexpr uint8_t EXIT_INVALID_FRAMES = 1;
    constexpr uint8_t WALL_ACQUIRE_FRAMES = 5;

    // A real opening jumps the reading a lot in one frame (straight to
    // invalid, or out to 300-500mm from a ~100mm wall lock); a gradual drift
    // creeps across EXIT_FAR_MM a little at a time. This threshold has a lot
    // of margin between the two.
    constexpr float SUDDEN_JUMP_MM = 100.0f;

    float         s_integral = 0.0f;
    float         s_lastError = 0.0f;
    unsigned long s_lastTime = 0;
    uint8_t       s_walledFrames = 0;
    uint8_t       s_farFrames = 0;
    uint8_t       s_invalidFrames = 0;
    float         s_lastValidDistanceMm = 0.0f;
    bool          s_hasLastValidDistance = false;
    Status        s_lastStatus = Status::NO_WALL;

    float obstacleTurnDeg() {
        // Only a still-blind search (never acquired a wall / not yet
        // re-acquired one) gets the narrower search turn; anything else
        // (actively following, or paused on a sudden candidate) gets the
        // normal wide turn.
        return (s_lastStatus == Status::NO_WALL) ? SEARCH_TURN_DEG : OBSTACLE_TURN_DEG;
    }

    void handleObstacle() {
        Drive::stop();
        Forward::forward(-OBSTACLE_BACKUP_SPEED, OBSTACLE_BACKUP_MM);
        Turn::turn(obstacleTurnDeg(), OBSTACLE_TURN_SPEED);
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

        Status status = Status::NO_WALL;
        if (armed && candidate) {
            // No numeric reading to compare for a dropout -> always sudden.
            // Otherwise compare against the last locked-on distance: a big
            // one-frame jump is sudden, anything smaller falls back to the
            // ordinary blind-search (NO_WALL) drive.
            bool sudden = !valid;
            if (valid && s_hasLastValidDistance) {
                sudden = (distanceMm - s_lastValidDistanceMm) >= SUDDEN_JUMP_MM;
            }
            if (sudden) status = Status::EXIT_CANDIDATE_SUDDEN;
        }

        if (DEBUG_WALL_FOLLOW) {
            Serial.printf("[WF] no-wall valid=%d dist=%.0f wall=%u far=%u inv=%u st=%d\n",
                          valid ? 1 : 0, distanceMm,
                          s_walledFrames, s_farFrames, s_invalidFrames,
                          (int)status);
        }
        return status;
    }

    void updateWallAcquired(float distanceMm) {
        if (s_walledFrames < WALL_ACQUIRE_FRAMES) ++s_walledFrames;
        resetExitCounters();
        s_lastValidDistanceMm = distanceMm;
        s_hasLastValidDistance = true;
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
    s_hasLastValidDistance = false;
    s_lastStatus = Status::NO_WALL;
    resetExitCounters();
}

Status tick(float targetMm, float baseSpeed) {
    if (Sensors::Touch::front()) {
        if (DEBUG_WALL_FOLLOW) Serial.println("[WF] touch recovery");
        handleObstacle();
        reset();
        return Status::TOUCH;
    }

    float distanceMm = 0.0f;
    const bool valid = readWallDistanceMm(distanceMm);
    const bool noWall = !valid || distanceMm >= EXIT_FAR_MM;

    if (noWall) {
        const Status status = classifyNoWall(valid, distanceMm);
        if (status == Status::EXIT_CANDIDATE_SUDDEN) {
            Drive::stop();
        } else {
            Drive::motor(baseSpeed, baseSpeed);
        }
        s_lastStatus = status;
        return status;
    }

    updateWallAcquired(distanceMm);

    const float correction = pidCorrection(distanceMm, targetMm);
    Drive::motor(baseSpeed + correction, baseSpeed - correction);
    s_lastStatus = Status::FOLLOWING;
    return Status::FOLLOWING;
}

}  // namespace WallFollow
}  // namespace Actions
