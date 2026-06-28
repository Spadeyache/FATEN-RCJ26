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

    // ISR — pushes current gains to servos every 9 ms.
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
//  Line-follow controller — slope-aware 4WD skid-steer.
//
//  IMU mount note (do NOT change IMU code): this mount reports
//    nose DOWN  → getRoll()  negative      left DOWN → getPitch() negative
//  so fore/aft tilt lands on getRoll() and sideways tilt on getPitch(), with the
//  signs inverted vs the robot frame. robotPitch()/robotRoll() do the axis-swap +
//  sign-flip to the conventional:
//    pitch > 0 → nose up        roll > 0 → LEFT side down  (roll < 0 → RIGHT down)
//
//  Slope compensation (gravAdj / rotAxisAdj / frictionCircAdj) is gated by
//  TILT_GATE_DEG: below the gate every helper is an identity / no-op, so flat-
//  ground racing runs symmetric with all four wheels full; the slope tune only
//  appears once tilt exceeds the gate.
//
//  Config split: universal constants (MAX_MOTOR_SPEED, pins, PRINT_PID) come from
//  config.h / pins_teensy.h. Everything in the namespace below is line-follow-only
//  tuning and lives here on purpose.
// =============================================================================
namespace {

enum Side  : uint8_t { SIDE_LEFT, SIDE_RIGHT };
enum Wheel : uint8_t { WHEEL_FL, WHEEL_FR, WHEEL_BL, WHEEL_BR };

inline bool wheelIsFront(Wheel w) { return w == WHEEL_FL || w == WHEEL_FR; }
inline bool wheelIsLeft (Wheel w) { return w == WHEEL_FL || w == WHEEL_BL; }

// IMU → robot-frame tilt remap for this mount (see header). Axis swap + sign flip.
inline float robotPitch() { return  Sensors::IMU::getRoll();  }   // + = nose up
inline float robotRoll()  { return -Sensors::IMU::getPitch(); }   // + = left side down

// --- Tilt gate ---------------------------------------------------------------
// Shared activation threshold. Below this tilt ALL slope compensation is a no-op
// and the controller is pure flat-ground PID; above it the slope tune kicks in.
constexpr float TILT_GATE_DEG = 8.0f;

// --- PID gains ---------------------------------------------------------------
// Flat ground uses the *_FLAT set; the moment frictionCircAdj detects a slope
// (drops the base speed below FRIC_SPEED_FLAT) the controller swaps to *_SLOPE.
constexpr float PID_KP_FLAT  = 1.5f;
constexpr float PID_KI_FLAT  = 0.0f;
constexpr float PID_KD_FLAT  = 0.0f;

constexpr float PID_KP_SLOPE = 0.85f;   // validated slope tune
constexpr float PID_KI_SLOPE = 0.0f;
constexpr float PID_KD_SLOPE = 0.65f;

constexpr float PID_INTEGRAL_LIMIT = 500.0f;
constexpr float PID_PITCH_GAIN     = 0.0f;   // fore/aft pitch → forward-speed bias
                                             // (line-follow tune; was config IMU_PITCH_GAIN)

// --- Tight-turn slow-down (driven by the XIAO TIGHT_SLOW flag) ----------------
constexpr float TIGHT_SLOW_BASE_SPEED   = 10.0f;
constexpr float TIGHT_SLOW_REVERSE_GAIN = 1.05f;

// --- gravAdj : roll-driven left/right power asymmetry ------------------------
constexpr float GRAV_GAIN_MIN  = 0.6f;   // upper-side correction gain at full tilt (1.0 = symmetric)
constexpr float GRAV_BOOST_MAX = 1.3f;   // upper-side reverse-bite boost at full tilt (1.0 = none)
constexpr float GRAV_ROLL_TAU  = 12.0f;  // deg: roll scale of the exponential saturation

// --- rotAxisAdj : rear-wheel de-rate to shift the rotation axis --------------
constexpr float ROTAXIS_PITCH_REF = 18.0f;  // deg : |pitch| at which the rear reaches ROTAXIS_REAR_MIN
constexpr float ROTAXIS_REAR_MIN  = 0.75f;  // gain: rear scale at full pitch (fore/aft axis shift)
constexpr float ROTAXIS_ROLL_REF  = 18.0f;  // deg : |roll| at which the downhill rear reaches ROTAXIS_DOWN_MIN
constexpr float ROTAXIS_DOWN_MIN  = 0.60f;  // gain: downhill-rear scale at full roll

// --- frictionCircAdj : slope base-speed selector -----------------------------
constexpr float FRIC_SPEED_FLAT = 70.0f;  // base speed on flat ground
constexpr float FRIC_SPEED_TILT = 40.0f;  // base speed once tilted past the gate

inline float rollGainFactor(float aRoll) {       // 1.0 → GRAV_GAIN_MIN as |roll| grows
    return 1.0f - (1.0f - GRAV_GAIN_MIN) * (1.0f - expf(-aRoll / GRAV_ROLL_TAU));
}
inline float rollBoostFactor(float aRoll) {      // 1.0 → GRAV_BOOST_MAX as |roll| grows
    return 1.0f + (GRAV_BOOST_MAX - 1.0f) * (1.0f - expf(-aRoll / GRAV_ROLL_TAU));
}

// gravAdj — roll-based left/right power. Below the gate: symmetric smooth
// differential (flat racing, gain 1.0, no fold). Above the gate: the *upper* side
// loses correction authority (→GRAV_GAIN_MIN) and snaps its inner wheel into
// reverse (the -25 fold + boost) to pivot steep turns using gravity. Reads roll.
float gravAdj(Side side, float base, float correction, float pitchAdj) {
    const float roll     = robotRoll();   // + = left side down
    const float aRoll    = fabsf(roll);
    const float corrTerm = (side == SIDE_LEFT) ? correction : -correction;

    if (aRoll <= TILT_GATE_DEG)
        return base + corrTerm + pitchAdj;

    const bool  isUpper = (side == SIDE_LEFT) ? (roll < 0.0f) : (roll > 0.0f);
    const float gain    = isUpper ? rollGainFactor(aRoll) : 1.0f;

    float speed = base + corrTerm * gain + pitchAdj;
    if (isUpper) {
        if (speed > -25.0f && speed < 25.0f) speed = -25.0f;   // fold the dead zone
        if (speed <= -25.0f) speed *= rollBoostFactor(aRoll);  // boost the reverse bite
    }
    return speed;
}

// rotAxisAdj — returns a 0..1 power scale for the given wheel, de-rating the rear
// to shift the rotation axis rearward. Identity below the gate (flat racing keeps
// all four wheels full). Reads pitch + roll.
//   pitch → de-rates BOTH rear wheels: 1.0 → ROTAXIS_REAR_MIN at ROTAXIS_PITCH_REF.
//   roll  → de-rates only the DOWNHILL rear: → ROTAXIS_DOWN_MIN at ROTAXIS_ROLL_REF.
//   combined by min(), so a full slope gives downhill 0.6 / uphill 0.75.
float rotAxisAdj(Wheel w) {
    const float pitch = robotPitch();   // + = nose up   (fore/aft)
    const float roll  = robotRoll();    // + = left down (sideways)
    const float tilt  = sqrtf(pitch * pitch + roll * roll);
    if (tilt < TILT_GATE_DEG) return 1.0f;
    if (wheelIsFront(w))      return 1.0f;   // front = reference axle

    // Pitch (fore/aft): both rear wheels 1.0 → ROTAXIS_REAR_MIN.
    const float pf = constrain(fabsf(pitch) / ROTAXIS_PITCH_REF, 0.0f, 1.0f);
    float scale    = 1.0f - (1.0f - ROTAXIS_REAR_MIN) * pf;

    // Roll: only the downhill rear wheel drops further toward ROTAXIS_DOWN_MIN.
    //   roll > 0 → LEFT side down ; roll < 0 → RIGHT side down.
    const bool isDown = wheelIsLeft(w) ? (roll > 0.0f) : (roll < 0.0f);
    if (isDown) {
        const float rf        = constrain(fabsf(roll) / ROTAXIS_ROLL_REF, 0.0f, 1.0f);
        const float downFloor = 1.0f - (1.0f - ROTAXIS_DOWN_MIN) * rf;   // 1.0 → 0.6
        scale = fminf(scale, downFloor);
    }
    return constrain(scale, 0.0f, 1.0f);
}

// frictionCircAdj — base-speed selector. On a slope (|pitch| or |roll| past the
// gate) there is less grip, so drop to FRIC_SPEED_TILT; otherwise run the fast
// flat-ground FRIC_SPEED_FLAT. The drop also triggers the *_SLOPE PID gain swap.
float frictionCircAdj() {
    const float pitch = robotPitch();   // + = nose up
    const float roll  = robotRoll();    // + = left down
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

void runLinePID() {
    static float integral  = 0.0f;
    static float lastError = 0.0f;
    static unsigned long lastTime = 0;

    const unsigned long now = micros();
    float dt = (now - lastTime) * 1e-6f;
    lastTime = now;
    if (dt <= 0.0f || dt > 0.5f) dt = 0.02f;   // first call / stall guard

    // Map XIAO line error 0..254 → ±200 so the PID gains match their tuned scale.
    const float rawError   = (Processing::XiaoDecode::lineError() - 127.0f) * (200.0f / 127.0f);
    const float derivative = (rawError - lastError) / dt;
    lastError = rawError;

    integral += rawError * dt;
    integral  = constrain(integral, -PID_INTEGRAL_LIMIT, PID_INTEGRAL_LIMIT);

    // Base speed: 70 on flat, 40 once tilted past the gate. The drop also selects
    // the *_SLOPE PID gains (flat → *_FLAT, slope → *_SLOPE).
    const float frictionBase = frictionCircAdj();
    const bool  slope = (frictionBase < FRIC_SPEED_FLAT);
    const float kp = slope ? PID_KP_SLOPE : PID_KP_FLAT;
    const float ki = slope ? PID_KI_SLOPE : PID_KI_FLAT;
    const float kd = slope ? PID_KD_SLOPE : PID_KD_FLAT;

    const float correction = kp * rawError + ki * integral + kd * derivative;

    const bool tightSlow = Processing::XiaoDecode::tightSlowFlag();
    const float base = tightSlow ? TIGHT_SLOW_BASE_SPEED : frictionBase;
    digitalWrite(LED_PIN, base == FRIC_SPEED_FLAT ? HIGH : LOW);   // LED on = flat-ground base speed

    // Fore/aft pitch bias — gated like the rest of the slope layer (and inert
    // while PID_PITCH_GAIN is 0).
    const float pitch    = robotPitch();   // + = nose up
    const float pitchAdj = (fabsf(pitch) > TILT_GATE_DEG) ? pitch * PID_PITCH_GAIN : 0.0f;

    // Steering speeds from gravAdj (roll-driven). Flat → symmetric smooth
    // differential; sideways tilt → upper-side de-rate + reverse-bite pivot.
    float leftSpeed  = gravAdj(SIDE_LEFT,  base, correction, pitchAdj);
    float rightSpeed = gravAdj(SIDE_RIGHT, base, correction, pitchAdj);

    leftSpeed  = constrain(leftSpeed,  -70.0f, base);
    rightSpeed = constrain(rightSpeed, -70.0f, base);

    // Split into four wheels and de-rate the rear via rotAxisAdj to shift the
    // rotation axis rearward (identity below the gate → flat racing keeps all
    // four wheels at full power).
    float fl = leftSpeed  * rotAxisAdj(WHEEL_FL);
    float fr = rightSpeed * rotAxisAdj(WHEEL_FR);
    float bl = leftSpeed  * rotAxisAdj(WHEEL_BL);
    float br = rightSpeed * rotAxisAdj(WHEEL_BR);

    if (tightSlow) {
        if (fl < 0.0f) fl *= TIGHT_SLOW_REVERSE_GAIN;
        if (fr < 0.0f) fr *= TIGHT_SLOW_REVERSE_GAIN;
        if (bl < 0.0f) bl *= TIGHT_SLOW_REVERSE_GAIN;
        if (br < 0.0f) br *= TIGHT_SLOW_REVERSE_GAIN;
    }

    motorRaw(fl, fr, bl, br);

#if PRINT_PID
    Serial.printf("PID err:%.1f drv:%.1f corr:%.1f base:%.0f L:%.0f R:%.0f\n",
                  rawError, derivative, correction, base, leftSpeed, rightSpeed);
#endif
}

float32_t frontLeftGain()  { return _flGain; }
float32_t frontRightGain() { return _frGain; }
float32_t backLeftGain()   { return _blGain; }
float32_t backRightGain()  { return _brGain; }

}  // namespace Drive
}  // namespace Actions
