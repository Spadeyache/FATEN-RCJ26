#include "globals.h"

// =============================================================================
//  Drive.ino — STS motor layer + Teensy-side line-follow PID
//  Exposes: initDrive(), motor(), runLinePID()
//  Internal: motorOutput() runs in IntervalTimer ISR every 9 ms
// =============================================================================

yacheSTS _sts;
IntervalTimer controlTimer;

// Gain variables written by motor() and consumed by the ISR — must be volatile.
volatile float32_t frontLeftGain  = 0.0f;
volatile float32_t frontRightGain = 0.0f;
volatile float32_t backLeftGain   = 0.0f;
volatile float32_t backRightGain  = 0.0f;

// ---------------------------------------------------------------------------
//  ISR — fires every 9 ms, pushes current gains to the STS servos.
//  Keep this as short as possible; _sts.power() is ~100 µs at 1 Mbps.
// ---------------------------------------------------------------------------
FASTRUN void motorOutput() {
    // Pitch compensation is read here if IMU is enabled.
    // float pitchAdj = pitch * IMU_PITCH_GAIN;
    // (apply pitchAdj to all four gains before calling power())

    _sts.power(frontLeftGain, frontRightGain, backLeftGain, backRightGain);
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

void initDrive() {
    _sts.begin(Serial2);
    _sts.setWheelMode(true);
    controlTimer.begin(motorOutput, 9000); // execute every 9 000 µs = 9 ms
}

// Thread-safe: disables interrupts while writing all four gains atomically.
FASTRUN void motor(float32_t left, float32_t right) {
    cli();
    frontLeftGain  = constrain(left,  -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    frontRightGain = constrain(right, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    backLeftGain   = constrain(left,  -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    backRightGain  = constrain(right, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    sei();
}

// ---------------------------------------------------------------------------
//  Teensy-side line-follow PID
//
//  xiaoLineError is the raw 0-254 position byte from XIAO (127 = centre).
//  We normalise it to ±200 before applying PID so the gain constants match
//  the original hand-tuned values.
//
//  Left motor gets PID_LEFT_SCALE applied to correction to compensate for
//  the camera's non-centred lighting response (see config.h).
// ---------------------------------------------------------------------------
void runLinePID() {
    static float integral      = 0.0f;
    static float lastError     = 0.0f;
    static float smoothedError = 0.0f;
    static float smoothedDeriv = 0.0f;
    static unsigned long lastTime = 0;

    unsigned long now = micros();
    float dt = (now - lastTime) * 1e-6f;
    lastTime = now;
    if (dt <= 0.0f || dt > 0.5f) dt = 0.02f;  // clamp: first call or stall

    // Map 0-254 → -200..+200 (positive = line is to the right → steer right)
    float rawError = (xiaoLineError - 127.0f) * (200.0f / 127.0f);

    // EMA smoothing on line error — damps sensor noise before PID sees it
    smoothedError = LINE_EMA_ALPHA * rawError + (1.0f - LINE_EMA_ALPHA) * smoothedError;

    // Derivative in error/second (frequency-independent) with EMA filter
    float rawDeriv = (smoothedError - lastError) / dt;
    smoothedDeriv  = DERIV_EMA_ALPHA * rawDeriv + (1.0f - DERIV_EMA_ALPHA) * smoothedDeriv;
    lastError = smoothedError;

    // Integral (correctly time-integrated; still off via Ki=0)
    integral += smoothedError * dt;
    integral  = constrain(integral, -PID_INTEGRAL_LIMIT, PID_INTEGRAL_LIMIT);

    float correction = PID_KP * smoothedError + PID_KI * integral + PID_KD * smoothedDeriv;

    // Optional pitch compensation — zero by default (IMU_PITCH_GAIN = 0)
    float pitchAdj = (float)pitch * IMU_PITCH_GAIN;

    float leftSpeed  = PID_BASE_SPEED + correction * PID_LEFT_SCALE + pitchAdj;
    float rightSpeed = PID_BASE_SPEED - correction                  + pitchAdj;

    motor(leftSpeed, rightSpeed);

    // Serial.printf("PID err: %.1f  sErr: %.1f  sDrv: %.1f  corr: %.1f  L: %.0f  R: %.0f\n",
    //               rawError, smoothedError, smoothedDeriv, correction, leftSpeed, rightSpeed);
}
