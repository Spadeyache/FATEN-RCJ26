#include "Drive.h"
#include "WeightDistribution.h"
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

// =============================================================================
//  IMU-driven per-wheel power adjustment helpers (used by runLinePID).
//
//  This IMU mount reports (measured — do NOT change IMU code):
//    nose DOWN  → Sensors::IMU::getRoll()  goes negative
//    left  DOWN → Sensors::IMU::getPitch() goes negative
//  So fore/aft tilt lands on getRoll() and left/right tilt on getPitch(), and
//  the signs are inverted vs the robot frame. robotPitch()/robotRoll() below do
//  the axis swap + sign flip so the rest of the logic keeps the conventional:
//    pitch > 0  → nose up
//    roll  > 0  → LEFT side down      (roll < 0 → RIGHT side down)
//
//  Design rule: every helper is CONTINUOUS in tilt and reduces to a no-op on
//  flat ground, so the *same* controller drives flat-line racing (symmetric,
//  smooth, all four wheels full) and steep sideways slopes (the validated tune
//  reappears as the saturated, large-roll limit).
// =============================================================================
namespace {

enum Side  : uint8_t { SIDE_LEFT, SIDE_RIGHT };
enum Wheel : uint8_t { WHEEL_FL, WHEEL_FR, WHEEL_BL, WHEEL_BR };

inline bool wheelIsFront(Wheel w) { return w == WHEEL_FL || w == WHEEL_FR; }
inline bool wheelIsLeft (Wheel w) { return w == WHEEL_FL || w == WHEEL_BL; }

// IMU → robot-frame tilt remap for this mount (see header). Axis swap + sign flip.
inline float robotPitch() { return  Sensors::IMU::getRoll();  }   // + = nose up
inline float robotRoll()  { return -Sensors::IMU::getPitch(); }   // + = left side down

// --- Line-follow PID tuning (migrated from config.h) -------------------------
//   Two gain sets, switched together with the base speed: the moment
//   frictionCircAdj drops the base below FRIC_SPEED_FLAT (i.e. on a slope), the
//   controller also swaps to the *_SLOPE gains. Flat ground uses the *_FLAT set.
constexpr float PID_KP_FLAT  = 1.5f; //1.5   // TODO: tune for fast flat-line racing
constexpr float PID_KI_FLAT  = 0.0f;
constexpr float PID_KD_FLAT  = 0.0f;

constexpr float PID_KP_SLOPE = 0.85f;   // validated slope tune
constexpr float PID_KI_SLOPE = 0.0f;
constexpr float PID_KD_SLOPE = 0.65f;

// Binary steep-turn speed drop. When the PID correction is this large, the
// robot slows its forward base so tight turns do not outrun the camera line.
constexpr float STEEP_TURN_CORR_THRESHOLD = 120.0f;
constexpr float STEEP_TURN_BASE_SPEED     = 10.0f;  
constexpr float TIGHT_SLOW_BASE_SPEED     = 10.0f;
constexpr float TIGHT_SLOW_REVERSE_GAIN   = 1.05f;

// Flat-surface PID tuning switch. When true, the line PID ignores IMU tilt for
// gain scheduling, gravity compensation, pitch adjustment, and rear-wheel
// rotation-axis scaling. The robot behaves as if pitch=roll=0 on every frame.
constexpr bool PID_DISABLE_IMU_GAIN_ADJUST = true;

constexpr float PID_INTEGRAL_LIMIT = 500.0f;
constexpr float LINE_EMA_ALPHA     = 0.3f;    // error smoothing  (currently disabled)
constexpr float DERIV_EMA_ALPHA    = 0.4f;    // deriv smoothing  (currently disabled)

// --- gravAdj tuning ----------------------------------------------------------  Driving sideways we want to differ the power of left and right.
constexpr float GRAV_GAIN_MIN      = 0.6f;   // upper-side correction gain at full tilt (1.0 = symmetric)
constexpr float GRAV_BOOST_MAX     = 1.3f;   // upper-side reverse-bite boost at full tilt (1.0 = none)
constexpr float GRAV_ROLL_TAU      = 12.0f;  // deg: roll scale of the exponential saturation
constexpr float GRAV_FOLD_TILT_DEG = 8.0f;   // below this |roll|, no -25 dead-zone fold / boost

// --- rotAxisAdj tuning -------------------------------------------------------
constexpr float ROTAXIS_TILT_DEG  = 8.0f;   // deg : total tilt gate (below → all wheels full)
constexpr float ROTAXIS_PITCH_REF = 18.0f;  // deg : |pitch| at which the rear reaches ROTAXIS_REAR_MIN
constexpr float ROTAXIS_REAR_MIN  = 0.75f;  // gain: rear scale at full pitch (fore/aft axis shift)
constexpr float ROTAXIS_ROLL_REF  = 18.0f;  // deg : |roll| at which the downhill rear reaches ROTAXIS_DOWN_MIN
constexpr float ROTAXIS_DOWN_MIN  = 0.60f;  // gain: downhill-rear scale at full roll

// --- frictionCircAdj tuning --------------------------------------------------
constexpr float FRIC_TILT_DEG   = 8.0f;   // |pitch| or |roll| past this counts as "on a slope"
constexpr float FRIC_SPEED_FLAT = 70.0f;  // 70 base speed on flat ground
constexpr float FRIC_SPEED_TILT = 40.0f;  // 55 base speed once tilted

inline float rollGainFactor(float aRoll) {       // 1.0 → GRAV_GAIN_MIN as |roll| grows
    return 1.0f - (1.0f - GRAV_GAIN_MIN) * (1.0f - expf(-aRoll / GRAV_ROLL_TAU));
}
inline float rollBoostFactor(float aRoll) {      // 1.0 → GRAV_BOOST_MAX as |roll| grows
    return 1.0f + (GRAV_BOOST_MAX - 1.0f) * (1.0f - expf(-aRoll / GRAV_ROLL_TAU));
}

inline float linePidPitch() {
    return PID_DISABLE_IMU_GAIN_ADJUST ? 0.0f : robotPitch();
}

inline float linePidRoll() {
    return PID_DISABLE_IMU_GAIN_ADJUST ? 0.0f : robotRoll();
}

// gravAdj — adjust the left right power to compensate the slop caused by gravity when robot turns. Reads roll.
//   Flat (roll≈0): both sides symmetric (gain 1.0, no fold) → smooth
//   differential for fast racing. Tilted: the *upper* side loses correction
//   authority (→GRAV_GAIN_MIN) and, past GRAV_FOLD_TILT_DEG, snaps its inner
//   wheel into reverse (the -25 fold + boost) to pivot steep turns using gravity.
float gravAdj(Side side, float base, float correction, float pitchAdj) {
    const float roll    = linePidRoll();   // + = left side down
    const float aRoll   = fabsf(roll);
    const bool  isUpper = (side == SIDE_LEFT) ? (roll < 0.0f) : (roll > 0.0f);

    const float corrTerm = (side == SIDE_LEFT) ? correction : -correction;
    const float gain     = isUpper ? rollGainFactor(aRoll) : 1.0f;

    float speed = base + corrTerm * gain + pitchAdj;

    if (isUpper && aRoll > GRAV_FOLD_TILT_DEG) {
        if (speed > -25.0f && speed < 25.0f) speed = -25.0f;   // fold the dead zone
        if (speed <= -25.0f) speed *= rollBoostFactor(aRoll);  // boost the reverse bite
    }
    return speed;
}

// rotAxisAdj — shifts the rotation axis by de-rating the rear wheels. (reads pitch and roll)
//   Returns a 0..1 power scale for the given wheel. Reads pitch + roll.
//   Identity below ROTAXIS_TILT_DEG, so flat racing runs all four at full power.
//     pitch → de-rates BOTH rear wheels: 1.0 → ROTAXIS_REAR_MIN at ROTAXIS_PITCH_REF.
//     roll  → de-rates only the DOWNHILL rear: → ROTAXIS_DOWN_MIN at ROTAXIS_ROLL_REF.
//   Combined by min(), so a full slope gives downhill 0.6 / uphill 0.75.
float rotAxisAdj(Wheel w) {
    const float pitch = linePidPitch();   // + = nose up   (fore/aft)
    const float roll  = linePidRoll();    // + = left down (sideways)
    const float tilt  = sqrtf(pitch * pitch + roll * roll);
    if (tilt < ROTAXIS_TILT_DEG) return 1.0f;
    if (wheelIsFront(w))         return 1.0f;   // front = reference axle

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

// frictionCircAdj — base-speed selector. Binary: if |pitch| or |roll| exceeds
//   FRIC_TILT_DEG (on a slope, less grip) drop to FRIC_SPEED_TILT, otherwise run
//   the fast flat-ground speed FRIC_SPEED_FLAT. Reads pitch + roll.
float frictionCircAdj() {
    const float pitch = linePidPitch();   // + = nose up
    const float roll  = linePidRoll();    // + = left down
    if (fabsf(pitch) > FRIC_TILT_DEG || fabsf(roll) > FRIC_TILT_DEG)
        return FRIC_SPEED_TILT;
        // digitalWrite(LED_PIN, HIGH);    // LED now shows TIGHT_SLOW flag (see runLinePID)
    // digitalWrite(LED_PIN, LOW);
    return FRIC_SPEED_FLAT;
}

}  // namespace

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

    // Base speed: frictionCircAdj picks 70 on flat / 55 once tilted past 8°.
    // With PID_DISABLE_IMU_GAIN_ADJUST=true, this always selects flat speed.
    const float frictionBase = frictionCircAdj();

    // Gain scheduling tied to the base speed: the moment frictionCircAdj slows
    // the base (i.e. we're on a slope), swap to the *_SLOPE gains. Flat → *_FLAT.
    const bool  slope = (frictionBase < FRIC_SPEED_FLAT);
    const float kp = slope ? PID_KP_SLOPE : PID_KP_FLAT;
    const float ki = slope ? PID_KI_SLOPE : PID_KI_FLAT;
    const float kd = slope ? PID_KD_SLOPE : PID_KD_FLAT;

    const bool tightSlow = Processing::XiaoDecode::tightSlowFlag();
    digitalWrite(LED_PIN, tightSlow ? HIGH : LOW);   // LED on = XIAO TIGHT_SLOW flag set
    const float correction = (kp * rawError + ki * integral + kd * derivative);
    // const bool  steepTurn = fabsf(correction) >= STEEP_TURN_CORR_THRESHOLD;
    const float base = tightSlow
        ? TIGHT_SLOW_BASE_SPEED
        : frictionBase;
    const float pitchAdj   = linePidPitch() * IMU_PITCH_GAIN;   // + = nose up

// Steering speeds come from gravAdj (roll-driven). Flat → symmetric smooth
// differential; sideways tilt → upper-side de-rate + reverse-bite pivot.
    float leftSpeed  = gravAdj(SIDE_LEFT,  base, correction, pitchAdj);
    float rightSpeed = gravAdj(SIDE_RIGHT, base, correction, pitchAdj);

    leftSpeed  = constrain(leftSpeed,  -70.0f, base);
    rightSpeed = constrain(rightSpeed, -70.0f, base);

    // Split into four wheels and de-rate the rear via rotAxisAdj to shift the
    // rotation axis rearward (identity below 8° tilt → flat racing keeps all
    // four wheels at full power).
    float fl = leftSpeed,  fr = rightSpeed;
    float bl = leftSpeed,  br = rightSpeed;

    fl *= rotAxisAdj(WHEEL_FL);
    fr *= rotAxisAdj(WHEEL_FR);
    bl *= rotAxisAdj(WHEEL_BL);
    br *= rotAxisAdj(WHEEL_BR);

    if (tightSlow) {
        if (fl < 0.0f) fl *= TIGHT_SLOW_REVERSE_GAIN;
        if (fr < 0.0f) fr *= TIGHT_SLOW_REVERSE_GAIN;
        if (bl < 0.0f) bl *= TIGHT_SLOW_REVERSE_GAIN;
        if (br < 0.0f) br *= TIGHT_SLOW_REVERSE_GAIN;
    }

    motorRaw(fl, fr, bl, br);


#if PRINT_PID
    Serial.printf("PID err:%.1f drv:%.1f corr:%.1f base:%.0f steep:%d L:%.0f R:%.0f\n",
                  rawError, derivative, correction, base, 0,
                  leftSpeed, rightSpeed);
#endif
}

float32_t frontLeftGain()  { return _flGain; }
float32_t frontRightGain() { return _frGain; }
float32_t backLeftGain()   { return _blGain; }
float32_t backRightGain()  { return _brGain; }

}  // namespace Drive
}  // namespace Actions
