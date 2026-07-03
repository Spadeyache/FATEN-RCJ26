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

bool turnUntilCenterPointImpl(float angleSign,
                              float speed,
                              unsigned long timeoutMs,
                              bool useTimeout) {
    const float l = (angleSign > 0) ?  speed : -speed;
    const float r = (angleSign > 0) ? -speed :  speed;
    const float turnNorm = (angleSign > 0) ? 1.0f : -1.0f;

#if PRINT_ACTIONS
    if (useTimeout) {
        Serial.printf("TurnCenterPoint: sign %.0f @ speed %.0f timeout %lu ms\n",
                      turnNorm, speed, timeoutMs);
    } else {
        Serial.printf("TurnCenterPoint: sign %.0f @ speed %.0f no timeout\n",
                      turnNorm, speed);
    }
#endif

    const unsigned long start = millis();
    unsigned long lastComms = 0;
    Drive::motorTurnGravityProfiled(l, r, turnNorm);

    while (!useTimeout || millis() - start < timeoutMs) {
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            Drive::motorTurnGravityProfiled(l, r, turnNorm);

            if (Sensors::XIAO_link::get(XIAO_REG_FEATURE) == FEAT_CENTER_POINT_BLACK) {
                Drive::stop();
                return true;
            }
            lastComms = millis();
        }
    }

    Drive::stop();
    return false;
}

}  // namespace

void turn(float angle_deg, float speed) {
    turnImpl(angle_deg, speed, true);
}

void turnRaw(float angle_deg, float speed) {
    turnImpl(angle_deg, speed, false);
}

// Arc-only variant: stops when the front ARC (top) sees a centered black run
// (XIAO_FLAG_TOP_LINE), ignoring the bottom row. Used by the evac-exit line
// spin - the exit line under the robot must not end the turn early.
bool turnUntilCenterPointArc(float angleSign, float speed, unsigned long timeoutMs) {
    const float l = (angleSign > 0) ?  speed : -speed;
    const float r = (angleSign > 0) ? -speed :  speed;
    const float turnNorm = (angleSign > 0) ? 1.0f : -1.0f;

    const unsigned long start = millis();
    unsigned long lastComms = 0;
    Drive::motorTurnGravityProfiled(l, r, turnNorm);

    while (millis() - start < timeoutMs) {
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            Drive::motorTurnGravityProfiled(l, r, turnNorm);

            if (Sensors::XIAO_link::get(XIAO_REG_FLAG) & XIAO_FLAG_TOP_LINE) {
                Drive::stop();
                return true;
            }
            lastComms = millis();
        }
    }

    Drive::stop();
    return false;
}

bool turnUntilCenterPoint(float angleSign, float speed, unsigned long timeoutMs) {
    return turnUntilCenterPointImpl(angleSign, speed, timeoutMs, true);
}

bool turnUntilCenterPoint(float angleSign, float speed) {
    return turnUntilCenterPointImpl(angleSign, speed, 0, false);
}

bool turnUntilCenterPointMaxDeg(float angleSign, float speed, float maxAngleDeg) {
    const unsigned long timeoutMs =
        (unsigned long)(fabsf(maxAngleDeg) * TURN_SPIN_MS_PER_DEG * MAX_MOTOR_SPEED / speed);
    return turnUntilCenterPointImpl(angleSign, speed, timeoutMs, true);
}

}  // namespace Turn
}  // namespace Actions
