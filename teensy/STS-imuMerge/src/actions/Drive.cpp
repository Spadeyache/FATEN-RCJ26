#include "Drive.h"
#include "config.h"
#include "../drivers/yacheSTS.h"
#include "../sensors/IMU.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>

namespace Actions {
namespace Drive {

namespace {
    yacheSTS       _sts;
    IntervalTimer  _controlTimer;

    // Volatile: written by motor() (any context), read by the ISR.
    volatile float32_t _flGain = 0.0f;
    volatile float32_t _frGain = 0.0f;
    volatile float32_t _blGain = 0.0f;
    volatile float32_t _brGain = 0.0f;

    // ISR — pushes current gains to servos every 9 ms.
    FASTRUN void motorOutput() {
        _sts.power(_flGain, _frGain, _blGain, _brGain);
    }
}

void init() {
    _sts.begin(Serial2);
    _sts.setWheelMode(true);
    _controlTimer.begin(motorOutput, 9000);
}

FASTRUN void motor(float32_t left, float32_t right) {
    cli();
    _flGain = constrain(left,  -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _frGain = constrain(right, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _blGain = constrain(left,  -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _brGain = constrain(right, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    sei();
}

FASTRUN void stop() { motor(0.0f, 0.0f); }

void runLinePID() {
    static float integral      = 0.0f;
    static float lastError     = 0.0f;
    static float smoothedError = 0.0f;
    static float smoothedDeriv = 0.0f;
    static unsigned long lastTime = 0;

    unsigned long now = micros();
    float dt = (now - lastTime) * 1e-6f;
    lastTime = now;
    if (dt <= 0.0f || dt > 0.5f) dt = 0.02f;   // first call / stall guard

    // Map 0..254 → ±200 so the PID gains match their hand-tuned scale.
    const float rawError = (Processing::XiaoDecode::lineError() - 127.0f) * (200.0f / 127.0f);

    smoothedError = LINE_EMA_ALPHA * rawError + (1.0f - LINE_EMA_ALPHA) * smoothedError;

    const float rawDeriv = (smoothedError - lastError) / dt;
    smoothedDeriv  = DERIV_EMA_ALPHA * rawDeriv + (1.0f - DERIV_EMA_ALPHA) * smoothedDeriv;
    lastError = smoothedError;

    integral += smoothedError * dt;
    integral  = constrain(integral, -PID_INTEGRAL_LIMIT, PID_INTEGRAL_LIMIT);

    const float correction = PID_KP * smoothedError + PID_KI * integral + PID_KD * smoothedDeriv;
    const float pitchAdj   = (float)Sensors::IMU::getPitch() * IMU_PITCH_GAIN;

    const float leftSpeed  = PID_BASE_SPEED + correction * PID_LEFT_SCALE + pitchAdj;
    const float rightSpeed = PID_BASE_SPEED - correction                  + pitchAdj;

    motor(leftSpeed, rightSpeed);

#if PRINT_PID
    Serial.printf("PID err:%.1f sErr:%.1f sDrv:%.1f corr:%.1f L:%.0f R:%.0f\n",
                  rawError, smoothedError, smoothedDeriv, correction, leftSpeed, rightSpeed);
#endif
}

float32_t frontLeftGain()  { return _flGain; }
float32_t frontRightGain() { return _frGain; }
float32_t backLeftGain()   { return _blGain; }
float32_t backRightGain()  { return _brGain; }

}  // namespace Drive
}  // namespace Actions
