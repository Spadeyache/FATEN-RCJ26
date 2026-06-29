#include "Drive.h"
#include "../../config.h"
#include "../../pins_teensy.h"
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

    // ISR - pushes current gains to servos every 9 ms.
    FASTRUN void motorOutput() {
        _sts.power(_flGain, _frGain, _blGain, _brGain);
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

FASTRUN void motor(float32_t left, float32_t right) {
    cli();
    _flGain = constrain(left,  -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _frGain = constrain(right, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _blGain = constrain(left,  -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _brGain = constrain(right, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    sei();
}

FASTRUN void stop() { motor(0.0f, 0.0f); }

// Spin in place, decaying linearly from |power| to 35 over durationMs, then stop.
void spinDecay(float32_t power, uint32_t durationMs) {
    const float32_t endSpd = 35.0f;
    const float32_t start  = fabsf(power);
    const uint32_t  stepMs = 20;

    for (uint32_t t = 0; t <= durationMs; t += stepMs) {
        float32_t spd = start + (endSpd - start) * (float32_t)t / (float32_t)durationMs;
        motor(spd, -spd);
        delay(stepMs);
    }
    stop();
}

FASTRUN void motorRaw(float32_t fl, float32_t fr, float32_t bl, float32_t br) {
    cli();
    _flGain = constrain(fl, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _frGain = constrain(fr, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _blGain = constrain(bl, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    _brGain = constrain(br, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
    sei();
}

// =============================================================================
//  Line-follow controller - slope-aware 4WD skid-steer.
//
//  IMU mount note (do NOT change IMU code): this mount reports
//    nose DOWN  -> getRoll()  negative      left DOWN -> getPitch() negative
//  so fore/aft tilt lands on getRoll() and sideways tilt on getPitch(), with the
//  signs inverted vs the robot frame. robotPitch()/robotRoll() do the axis-swap +
//  sign-flip to the conventional:
//    pitch > 0 -> nose up        roll > 0 -> LEFT side down  (roll < 0 -> RIGHT down)
//
//  Layout (top to bottom):
//    1. DEV angle hardcode  - bench testing without the IMU.
//    2. Tuning constants    - one flat block, edit in place.
//    3. rotAxisBias()       - continuous fore/aft pivot shift from error + tilt.
//    4. runLinePID()        - the controller.
//
//  Everything below the gate (|tilt| < TILT_GATE_DEG) is plain flat-ground PID
//  with all four wheels equal; the slope layer only appears past the gate.
// =============================================================================
namespace {

// =============================================================================
//  1. DEV - angle hardcode for bench testing
//
//  DEV_FORCE_TILT = true feeds fixed pitch/roll instead of the IMU, so slope
//  behaviour can be tested on a flat bench. Set false for normal IMU use.
//    pitch + = nose up        roll + = left side down
// =============================================================================
constexpr bool  DEV_FORCE_TILT      = false;
constexpr float DEV_ROBOT_PITCH_DEG = 0.0f;
constexpr float DEV_ROBOT_ROLL_DEG  = 25.0f;

// IMU -> robot-frame remap for this mount (axis swap + sign flip, see header).
inline float robotPitch() { return DEV_FORCE_TILT ? DEV_ROBOT_PITCH_DEG : Sensors::IMU::getRoll(); }
inline float robotRoll()  { return DEV_FORCE_TILT ? DEV_ROBOT_ROLL_DEG  : -Sensors::IMU::getPitch(); }

// =============================================================================
//  2. Tuning constants
//  Slope layer activates once |pitch| or |roll| exceeds TILT_GATE_DEG.
// =============================================================================

// --- Tilt gate + base speed --------------------------------------------------
constexpr float TILT_GATE_DEG   = 12.0f;
constexpr float FRIC_SPEED_FLAT = 70.0f;   // flat-ground base speed
constexpr float FRIC_SPEED_TILT = 40.0f;   // base speed once tilted past the gate

// --- PID (flat vs slope; selected by the gate, NOT by error sign) ------------
constexpr float PID_KP_FLAT  = 1.5f,  PID_KI_FLAT  = 0.0f, PID_KD_FLAT  = 1.0f;
constexpr float PID_KP_SLOPE = 0.85f, PID_KI_SLOPE = 0.0f, PID_KD_SLOPE = 0.65f;
constexpr float PID_INTEGRAL_LIMIT = 500.0f;

// --- Tight-turn slow-down (driven by the XIAO TIGHT_SLOW flag) ---------------
constexpr float TIGHT_SLOW_BASE_SPEED   = 10.0f;
constexpr float TIGHT_SLOW_REVERSE_GAIN = 1.05f;

// --- Side-roll left/right power (|roll| > gate) ------------------------------
// Hardcoded, symmetric for left-down / right-down: the UPPER wheels lose power.
constexpr float ROLL_UPPER_GAIN = 0.7f;   // upper-side speed multiplier

// --- rot-axis : continuous fore/aft pivot shift ------------------------------
// rotAxisBias() returns a signed value in [-1, +1]:
//   bias > 0  -> pivot FORWARD  (de-rate FRONT wheels toward ROTAXIS_FRONT_MIN)
//   bias < 0  -> pivot BACK     (de-rate REAR  wheels toward ROTAXIS_REAR_MIN)
// Continuous in the XIAO line error (sign + magnitude), not bucketed.
//   nose UP   : sharper curve -> more forward ; gentle -> neutral
//   nose DOWN : pivot back regardless of curve direction
//   side roll : curve toward the DOWNHILL side -> forward ; otherwise -> back
constexpr float ROTAXIS_PITCH_REF  = 18.0f;  // deg: |pitch| for full fore/aft effect
constexpr float ROTAXIS_ROLL_REF   = 18.0f;  // deg: |roll|  for full side effect
constexpr float ROTAXIS_NOSE_UP    = 1.0f;   // nose-up forward strength  (x pitch x |err|)
constexpr float ROTAXIS_NOSE_DN    = 1.0f;   // nose-down back strength   (x pitch)
constexpr float ROTAXIS_ROLL_FWD   = 1.0f;   // downhill-curve forward strength (x roll x err)
constexpr float ROTAXIS_ROLL_BACK  = 0.6f;   // otherwise back strength   (x roll)
constexpr float ROTAXIS_FRONT_MIN  = 0.6f;   // front wheel scale at full forward bias
constexpr float ROTAXIS_REAR_MIN   = 0.6f;   // rear  wheel scale at full back bias
constexpr float ROLL_DOWNHILL_SIGN = 1.0f;   // flip to -1 if downhill mapping is reversed

// =============================================================================
//  3. rotAxisBias - signed pivot shift in [-1, +1] from pitch/roll + line error.
//  eNorm = line error normalised to [-1, +1] (sign = steer direction).
// =============================================================================
float rotAxisBias(float eNorm) {
    const float pitch = robotPitch();   // + = nose up
    const float roll  = robotRoll();    // + = left side down
    const float aErr  = fabsf(eNorm);
    float bias = 0.0f;

    // Fore/aft pitch.
    if (pitch > TILT_GATE_DEG) {                 // nose up: sharp -> forward, gentle -> neutral
        const float pf = constrain(pitch / ROTAXIS_PITCH_REF, 0.0f, 1.0f);
        bias += ROTAXIS_NOSE_UP * pf * aErr;
    } else if (pitch < -TILT_GATE_DEG) {         // nose down: back, both curve directions
        const float pf = constrain(-pitch / ROTAXIS_PITCH_REF, 0.0f, 1.0f);
        bias -= ROTAXIS_NOSE_DN * pf;
    }

    // Side roll (downhill-relative so left-down / right-down stay symmetric).
    if (fabsf(roll) > TILT_GATE_DEG) {
        const float rf = constrain(fabsf(roll) / ROTAXIS_ROLL_REF, 0.0f, 1.0f);
        const float downhillErr = eNorm * ROLL_DOWNHILL_SIGN * (roll > 0.0f ? 1.0f : -1.0f);
        if (downhillErr > 0.0f) bias += ROTAXIS_ROLL_FWD  * rf * downhillErr;  // toward downhill -> forward
        else                    bias -= ROTAXIS_ROLL_BACK * rf;                // otherwise -> back
    }
    return constrain(bias, -1.0f, 1.0f);
}

// frictionCircAdj - base-speed selector. On a slope (past the gate) there is less
// grip, so drop to FRIC_SPEED_TILT; otherwise run the fast flat-ground speed.
// The drop also triggers the *_SLOPE PID gain swap in runLinePID().
float frictionCircAdj() {
    const float pitch = robotPitch();
    const float roll  = robotRoll();
    if (fabsf(pitch) > TILT_GATE_DEG || fabsf(roll) > TILT_GATE_DEG)
        return FRIC_SPEED_TILT;
    return FRIC_SPEED_FLAT;
}

float currentLinePidBase() {
    if (Processing::XiaoDecode::tightSlowFlag())
        return TIGHT_SLOW_BASE_SPEED;
    return frictionCircAdj();
}

}  // namespace

uint32_t scaledLinePidMs(uint32_t flatMs, uint32_t minMs, uint32_t maxMs) {
    const float base = fmaxf(currentLinePidBase(), 1.0f);
    const uint32_t scaled = (uint32_t)((float)flatMs * FRIC_SPEED_FLAT / base + 0.5f);
    return constrain(scaled, minMs, maxMs);
}

// =============================================================================
//  4. runLinePID - the controller.
// =============================================================================
void runLinePID() {
    static float integral  = 0.0f;
    static float lastError = 0.0f;
    static unsigned long lastTime = 0;

    const unsigned long now = micros();
    float dt = (now - lastTime) * 1e-6f;
    lastTime = now;
    if (dt <= 0.0f || dt > 0.5f) dt = 0.02f;   // first call / stall guard

    // Map XIAO line error 0..254 -> +-200 so the PID gains match their tuned scale.
    const float rawError   = (Processing::XiaoDecode::lineError() - 127.0f) * (200.0f / 127.0f);
    const float eNorm      = rawError / 200.0f;            // [-1, +1], sign = steer direction
    const float derivative = (rawError - lastError) / dt;
    lastError = rawError;

    integral += rawError * dt;
    integral  = constrain(integral, -PID_INTEGRAL_LIMIT, PID_INTEGRAL_LIMIT);

    // Base speed: flat vs slope (slope = tilted past the gate). The slope flag
    // also selects the *_SLOPE PID gains.
    const float frictionBase = frictionCircAdj();
    const bool  slope = (frictionBase < FRIC_SPEED_FLAT);
    const float kp = slope ? PID_KP_SLOPE : PID_KP_FLAT;
    const float ki = slope ? PID_KI_SLOPE : PID_KI_FLAT;
    const float kd = slope ? PID_KD_SLOPE : PID_KD_FLAT;

    const float correction = kp * rawError + ki * integral + kd * derivative;

    const bool  tightSlow = Processing::XiaoDecode::tightSlowFlag();
    const float base = tightSlow ? TIGHT_SLOW_BASE_SPEED : frictionBase;
    digitalWrite(LED_PIN, base == FRIC_SPEED_FLAT ? HIGH : LOW);   // LED on = flat-ground base speed

    // Left/right steering speeds.
    float leftSpeed  = base + correction;
    float rightSpeed = base - correction;

    // Side roll past the gate: the UPPER side loses power (hardcoded x0.7,
    // symmetric for left-down / right-down). roll > 0 = left down -> right is upper.
    const float roll = robotRoll();
    if (fabsf(roll) > TILT_GATE_DEG) {
        if (roll > 0.0f) rightSpeed *= ROLL_UPPER_GAIN;
        else             leftSpeed  *= ROLL_UPPER_GAIN;
    }

    leftSpeed  = constrain(leftSpeed,  -70.0f, base);
    rightSpeed = constrain(rightSpeed, -70.0f, base);

    // rot axis: continuous fore/aft pivot shift. bias > 0 de-rates the front
    // (pivot forward), bias < 0 de-rates the rear (pivot back). Zero below the gate.
    const float bias = rotAxisBias(eNorm);
    float frontScale = 1.0f;
    float rearScale  = 1.0f;
    if (bias > 0.0f)      frontScale = 1.0f - bias * (1.0f - ROTAXIS_FRONT_MIN);
    else if (bias < 0.0f) rearScale  = 1.0f + bias * (1.0f - ROTAXIS_REAR_MIN);  // bias < 0

    float fl = leftSpeed  * frontScale;
    float fr = rightSpeed * frontScale;
    float bl = leftSpeed  * rearScale;
    float br = rightSpeed * rearScale;

    if (tightSlow) {
        if (fl < 0.0f) fl *= TIGHT_SLOW_REVERSE_GAIN;
        if (fr < 0.0f) fr *= TIGHT_SLOW_REVERSE_GAIN;
        if (bl < 0.0f) bl *= TIGHT_SLOW_REVERSE_GAIN;
        if (br < 0.0f) br *= TIGHT_SLOW_REVERSE_GAIN;
    }

    motorRaw(fl, fr, bl, br);

#if PRINT_PID
    Serial.printf("PID err:%.1f corr:%.1f base:%.0f bias:%.2f L:%.0f R:%.0f\n",
                  rawError, correction, base, bias, leftSpeed, rightSpeed);
#endif
}

float32_t frontLeftGain()  { return _flGain; }
float32_t frontRightGain() { return _frGain; }
float32_t backLeftGain()   { return _blGain; }
float32_t backRightGain()  { return _brGain; }

}  // namespace Drive
}  // namespace Actions
