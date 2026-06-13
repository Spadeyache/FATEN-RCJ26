#include "Drive.h"
#include "WeightDistribution.h"
#include "../../config.h"
#include "../../pins_teensy.h"
#include "../drivers/yacheSTS.h"
#include "../sensors/IMU.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>

// === SLOPE_TEST: hardcoded ~25° sideways-traverse experiment ===========
//  0 = identical to flat-ground behavior.
//  1 = soften steering (shrink the L/R turn differential) and slow forward
//      speed, to test whether gentler/slower corrections stop the nose-up.
//  No IMU read — the 25° traverse is assumed/hardcoded by enabling this.
#define SLOPE_TEST 0
const float CORRECTION_SCALE = 0.3f;  // 1.0 = normal turn, 0.0 = drive straight
const float SPEED_SCALE      = 0.5f;  // forward speed multiplier on the slope
const float TURN_REAR_SCALE  = 1.4f;  // turn applied to REAR wheels relative to front:
                                      // 1.0 = pivot at center, >1.0 = rear out-turns front
                                      // so the rear bites and the axis shifts back (front
                                      // slides); <1.0 = axis shifts forward

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

    // ISR â€” pushes current gains to servos every 9 ms.
    FASTRUN void motorOutput() {
        _sts.power(_flGain, _frGain, _blGain, _brGain);
    }

    // Map signed angle (deg) to WeightDistribution table index, then to 0..1 gain.
    //   table layout: 36 cells, -35°..+35° step 2°, no 0° entry.
    //   idx = (angle + 35) / 2, clamped to [0, 35].
    FASTRUN float32_t tiltGain(const uint8_t (&table)[36], float32_t angleDeg) {
        int idx = (int)((angleDeg + 35.0f) * 0.5f);
        if (idx < 0)  idx = 0;
        if (idx > 35) idx = 35;
        return table[idx] * (1.0f / 255.0f);
    }
}

void init() {
    // Enable the STS bus 74HCT126 buffer (held HIGH; write-only bus).
    pinMode(STS_EN_PIN, OUTPUT);
    digitalWrite(STS_EN_PIN, HIGH);

    _sts.begin(STS_SERIAL);
    _sts.setWheelMode(true);
    _controlTimer.begin(motorOutput, 9000);
}

FASTRUN void motor(float32_t left, float32_t right, bool imuCompensation) {
    float32_t fl = left, fr = right, bl = left, br = right;

    if (imuCompensation) {
        // Multiplicative tilt compensation. Each table cell is a 0..1 gain
        // applied to the *unloaded* pair of wheels (the side that has lifted
        // and would otherwise spin/waste power). The signed table layout lets
        // you encode different gains for + vs - tilt.
        //
        //   Pitch > 0  (nose up)     → front wheels unloaded → scale FL, FR
        //   Pitch < 0  (nose down)   → back  wheels unloaded → scale BL, BR
        //   Roll  > 0  (right down)  → left  wheels unloaded → scale FL, BL
        //   Roll  < 0  (left  down)  → right wheels unloaded → scale FR, BR
        const float32_t pitch = Sensors::IMU::getPitch();
        const float32_t pitchGain = tiltGain(pitchDistribution, pitch);

        if (pitch >= 0.0f) { fl *= pitchGain; fr *= pitchGain; }
        else               { bl *= pitchGain; br *= pitchGain; }

        // Roll compensation disabled — uncomment to re-enable:
        // const float32_t roll  = Sensors::IMU::getRoll();
        // const float32_t rollGain  = tiltGain(rollDistribution,  roll);
        // if (roll  >= 0.0f) { fl *= rollGain;  bl *= rollGain;  }
        // else               { fr *= rollGain;  br *= rollGain;  }
    }

    cli();
    _flGain = constrain(fl /* *0.4 */, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _frGain = constrain(fr/* * 0.4 */, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _blGain = constrain(bl, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _brGain = constrain(br, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    // if(_flGain > 0){_flGain *= 0.6;}
    // if(_frGain > 0){_frGain *= 0.6;}
    // if(_blGain > 0){_blGain *= 0.6;}
    // if(_brGain > 0){_brGain *= 0.6;}
    sei();
}

FASTRUN void stop() { motor(0.0f, 0.0f); }

FASTRUN void motorRaw(float32_t fl, float32_t fr, float32_t bl, float32_t br) {
    cli();
    _flGain = constrain(fl, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _frGain = constrain(fr, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _blGain = constrain(bl, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _brGain = constrain(br, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    sei();
}

void runLinePID() {
    static float integral      = 0.0f;
    static float lastError     = 0.0f;
    // static float smoothedError = 0.0f;   // smoothing disabled
    // static float smoothedDeriv = 0.0f;   // smoothing disabled
    static unsigned long lastTime = 0;

    unsigned long now = micros();
    float dt = (now - lastTime) * 1e-6f;
    lastTime = now;
    if (dt <= 0.0f || dt > 0.5f) dt = 0.02f;   // first call / stall guard

    // Map 0..254 â†’ Â±200 so the PID gains match their hand-tuned scale.
    const float rawError = (Processing::XiaoDecode::lineError() - 127.0f) * (200.0f / 127.0f);

    // --- Smoothing disabled: plain PID on the raw error/derivative. ---
    // smoothedError = LINE_EMA_ALPHA * rawError + (1.0f - LINE_EMA_ALPHA) * smoothedError;
    //
    // const float rawDeriv = (smoothedError - lastError) / dt;
    // smoothedDeriv  = DERIV_EMA_ALPHA * rawDeriv + (1.0f - DERIV_EMA_ALPHA) * smoothedDeriv;
    // lastError = smoothedError;
    //
    // integral += smoothedError * dt;
    // integral  = constrain(integral, -PID_INTEGRAL_LIMIT, PID_INTEGRAL_LIMIT);
    //
    // const float correction = PID_KP * smoothedError + PID_KI * integral + PID_KD * smoothedDeriv;

    const float derivative = (rawError - lastError) / dt;
    lastError = rawError;

    integral += rawError * dt;
    integral  = constrain(integral, -PID_INTEGRAL_LIMIT, PID_INTEGRAL_LIMIT);

    const float correction = PID_KP * rawError + PID_KI * integral + PID_KD * derivative;
    const float pitchAdj   = (float)Sensors::IMU::getPitch() * IMU_PITCH_GAIN;

#if SLOPE_TEST
    const float turn = correction * CORRECTION_SCALE;   // shrink the L/R turn differential
    const float base = PID_BASE_SPEED * SPEED_SCALE;    // slow down on the slope

    // Move the rotation axis rearward: front wheels get the full turn, rear
    // wheels get a reduced turn (smaller L/R differential), so the front swings
    // more and the pivot shifts back toward the rear axle.
    const float turnF = turn;                    // front L/R differential
    const float turnR = turn * TURN_REAR_SCALE;  // rear  L/R differential (reduced)

    const float fl = base + turnF * PID_LEFT_SCALE + pitchAdj;
    const float fr = base - turnF                  + pitchAdj;
    const float bl = base + turnR * PID_LEFT_SCALE + pitchAdj;
    const float br = base - turnR                  + pitchAdj;

    motorRaw(fl, fr, bl, br);
#else
    const float leftSpeed  = PID_BASE_SPEED + correction + pitchAdj;
    const float rightSpeed = PID_BASE_SPEED - correction + pitchAdj;

    motor(leftSpeed, rightSpeed);
#endif

#if PRINT_PID
    Serial.printf("PID err:%.1f drv:%.1f corr:%.1f L:%.0f R:%.0f\n",
                  rawError, derivative, correction, leftSpeed, rightSpeed);
#endif
}

float32_t frontLeftGain()  { return _flGain; }
float32_t frontRightGain() { return _frGain; }
float32_t backLeftGain()   { return _blGain; }
float32_t backRightGain()  { return _brGain; }

}  // namespace Drive
}  // namespace Actions
