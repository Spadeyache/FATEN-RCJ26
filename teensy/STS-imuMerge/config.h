#pragma once

#include <stdint.h>

// =============================================================================
//  config.h — Tunable constants + serial-print toggles
//
//  No pin numbers here (see pins_teensy.h).
//  No code here — only #defines and enums.
// =============================================================================

// =============================================================================
//  Serial-print toggles — set to 0 to silence that module
// =============================================================================
#define PRINT_STATE      1   // state transitions
#define PRINT_IMU        0   // pitch/roll/yaw at 10 Hz
#define PRINT_XIAO       0   // CommandFilter votes + xiaoCommand
#define PRINT_K230       0   // K230 detections
#define PRINT_PID        0   // line PID internals
#define PRINT_MAPPING    0   // mapping/EKF/checkpoint logs
#define PRINT_ACTIONS    0   // turn / forward / arm action logs
#define PRINT_TOUCH      0   // touchfront + conduct triggers

// =============================================================================
//  Serial baud rates + protocol IDs
// =============================================================================

#define XIAO_BAUD           4000000UL

// XIAO register IDs — shared protocol with xiaoesp/XIAOdev/config.h. KEEP IN SYNC.
//   reg 0x01 FEATURE  X→T  per-frame event (FEAT_* below)
//   reg 0x02 COM      X→T  line error 0..254 (127 = centred)
//   reg 0x03 MODE     T→X  active XIAO mode
//   reg 0x05 FLAG     X→T  bit0 commit, bit1 tight-turn slow drive
#define XIAO_REG_FEATURE    0x01
#define XIAO_REG_COM        0x02
#define XIAO_REG_MODE       0x03
#define XIAO_REG_FLAG       0x05

#define XIAO_FLAG_COMMIT      0x01
#define XIAO_FLAG_TIGHT_SLOW  0x02
#define XIAO_FLAG_BOTTOM_LINE 0x04
#define XIAO_FLAG_TOP_LINE    0x20

// FEATURE byte — LINE-follow events (the clean contract; keep in sync with XIAO):
#define FEAT_NONE           0
#define FEAT_UTURN          1   // XIAO GreenFilter-confirmed both-green
#define FEAT_SILVER         3   // raw silver on the scan row (Teensy filters)
#define FEAT_BLACK_INTERSECT 6   // saturated black row in LINE mode; suppress green rereads
#define FEAT_GREEN_LEFT     7   // XIAO GreenFilter-confirmed left turn  (hardcoded fwd+turn)
#define FEAT_GREEN_RIGHT    8   // XIAO GreenFilter-confirmed right turn (hardcoded fwd+turn)

// FEATURE byte — mode-scoped codes for SEARCH_LINE / NOGI modes (separate code space):
#define FEAT_CENTER_POINT_BLACK 1   // CENTER_POINT mode: front-arc black point centered
#define FEAT_SEARCH_LINE_SILVER 5   // SEARCH_LINE mode: silver tape
#define FEAT_SEARCH_LINE_BLACK  6   // SEARCH_LINE mode: black return line

enum XiaoMode : uint8_t {
    XIAO_MODE_LINE        = 0,
    XIAO_MODE_SEARCH_LINE = 1,
    XIAO_MODE_NOGI        = 2,
    XIAO_MODE_EVAC_COLOR_MASK = 5,   // evac entry/exit: silver side scan + row-45 black flag
    XIAO_MODE_CENTER_POINT = 6,      // front arc: feature=1 when black point is centered
};

// =============================================================================
//  Motor
// =============================================================================
#define MAX_MOTOR_SPEED     100

// Shared line-follow base speeds. Drive.cpp uses these for PID base speed;
// LINE_Follow.cpp intersection forward moves use the same values by state.
#define LINE_FOLLOW_BASE_SPEED_FLAT   70.0f
#define LINE_FOLLOW_BASE_SPEED_NOSE_UP 70.0f
#define LINE_FOLLOW_BASE_SPEED_NOSE_DOWN 50.0f
#define LINE_FOLLOW_BASE_SPEED_SLOPE  55.0f //55

// Nose-down sharp-correction base: replaces NOSE_DOWN base while the line PID
// error magnitude exceeds the threshold (raw error scale is +-200).
#define LINE_FOLLOW_BASE_SPEED_NOSE_DOWN_SHARP 20.0f
#define LINE_FOLLOW_NOSE_DOWN_SHARP_ERR        40.0f

// =============================================================================
//  IMU
// =============================================================================
#define CALIBRATE_IMU       0   // 1 = calibrate at boot + save to EEPROM; 0 = load saved EEPROM offsets
#define IMU_SAMPLE_RATE     200.0f
#define IMU_EMA_ALPHA       0.25f
// (line-follow pitch→speed gain moved to PID_PITCH_GAIN in src/actions/Drive.cpp)

// MPU6050 calibration offsets — fallback / initial-guess values (from IMU-01).
// At boot they are overwritten by either the auto-calibration (CALIBRATE_IMU==1)
// or the values loaded from EEPROM (CALIBRATE_IMU==0).
// #define IMU_AX_OFFSET       -4737
// #define IMU_AY_OFFSET        -374
// #define IMU_AZ_OFFSET         631
// #define IMU_GX_OFFSET          19
// #define IMU_GY_OFFSET          54
// #define IMU_GZ_OFFSET           2

#define IMU_AX_OFFSET       -894
#define IMU_AY_OFFSET        -1913
#define IMU_AZ_OFFSET         1031
#define IMU_GX_OFFSET          207
#define IMU_GY_OFFSET          -46
#define IMU_GZ_OFFSET           -52

// =============================================================================
//  Robot geometry — shared unit reference for Forward and Turn
// =============================================================================
#define WHEEL_DIAMETER_MM      70.0f   // tyre outer diameter (mm) — physical reference
                                       // Both Forward and Turn calibration constants are
                                       // empirical at MAX_MOTOR_SPEED and scale linearly:
                                       //   duration = constant × MAX_MOTOR_SPEED / speed

// =============================================================================
//  Action: Turn — universal spin-in-place
//   turn(angle_deg, speed = MAX_MOTOR_SPEED)
//     angle_deg > 0 → right   |   angle_deg < 0 → left
//     duration = |angle| × TURN_SPIN_MS_PER_DEG × MAX_MOTOR_SPEED / speed
// =============================================================================
#define TURN_SPIN_MS_PER_DEG   7.3f   // calibrated at MAX_MOTOR_SPEED, in-place spin

// =============================================================================
//  Action: Forward — distance/time calibration
//   forward(speed, distance_mm)
//     duration = distance_mm × FORWARD_MS_PER_MM × MAX_MOTOR_SPEED / speed
// =============================================================================
#define FORWARD_MS_PER_MM   4.4f   //now tuned for 50
#define FORWARD_YAW_KP      1.5f  //not in use

// =============================================================================
//  State machine timings
// =============================================================================

// EVAC shared tuning: used by EVAC victim approach and VictimManager grab
// self-confirm, so it stays here.
#define EVAC_GRAB_STOP_HEIGHT_PX     135.0f
// Other EVAC tuning is file-local in EVAC.cpp / VictimManager.cpp.

// =============================================================================
//  CommandFilter — moving-average vote thresholds (votes within the last
//  FILTER_QUEUE_SIZE frames needed to confirm each event)
// =============================================================================
#define FILTER_QUEUE_SIZE        15
#define FILTER_THRESHOLD_SILVER   4   // silver (evac entry)
#define FILTER_THRESHOLD_BLACK_INTERSECT 5  // saturated black row before/through an intersection
#define FILTER_THRESHOLD_GREEN    6   // green left/right (matches main); u-turn = both sides build up

// After firing any green turn (u-turn / left / right), ignore all green for this
// long so the same intersection isn't re-read on the way out.
#define DISABLE_GREEN_MS         1000

// Right-green counting: every confirmed right green bumps a counter (persists
// for the whole run; reset at power-on). The robot only TURNS on the marker
// whose count equals one of these two values — the name says which arm gets
// released at the wall (front touch) after that turn. No ordering guarantee:
// LEFT may be higher or lower than RIGHT. All other right greens are driven
// straight through.
#define GREEN_RIGHT_TURN_COUNT_LEFT   1   // this Nth right green → turn, deploy LEFT arm
#define GREEN_RIGHT_TURN_COUNT_RIGHT  4   // this Nth right green → turn, deploy RIGHT arm

// Deploy run: after the deploy turn, front touch = wall. Release the pending
// arm, stop this long, spin 180°, resume line follow.
#define DEPLOY_TOUCH_STOP_MS       6000
#define DEPLOY_UTURN_SPEED         60.0f

// End of run: once the right-green count has passed BOTH trigger counts, every
// further right green is the end marker — stop this long.
#define END_RIGHT_GREEN_STOP_MS   12000

#define BLACK_INTERSECT_DISABLE_GREEN_BASE_MS 1100
#define BLACK_INTERSECT_DISABLE_GREEN_MIN_MS   700
#define BLACK_INTERSECT_DISABLE_GREEN_MAX_MS  2000

// =============================================================================
//  Intersection / green-marker action tuning — FLAT ONLY
//
//  The IMU slope layer is disabled (DEV_FORCE_TILT in Drive.cpp); the slope
//  variants (nose up/down, side up/downhill) were removed — see git history.
//
//  Forward distance/speed runs before the turn.
// =============================================================================

#define INTERSECTION_GREEN_LEFT_FLAT_FORWARD_SPEED        LINE_FOLLOW_BASE_SPEED_FLAT
#define INTERSECTION_GREEN_LEFT_FLAT_FORWARD_MM           52.0f
#define INTERSECTION_GREEN_LEFT_FLAT_TURN_ANGLE          -90.0f
#define INTERSECTION_GREEN_LEFT_FLAT_TURN_SPEED           60.0f

#define INTERSECTION_GREEN_RIGHT_FLAT_FORWARD_SPEED       LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_GREEN_RIGHT_FLAT_FORWARD_MM          52.0f
#define INTERSECTION_GREEN_RIGHT_FLAT_TURN_ANGLE          90.0f
#define INTERSECTION_GREEN_RIGHT_FLAT_TURN_SPEED          60.0f

// Green turn finish:
//   timed turn does all but this many degrees, then CENTER_POINT mode finishes
//   by spinning until the front-arc black point is centered. The finish is
//   time-capped to this same degree budget, so it cannot pass the original
//   configured turn angle (90 deg on the flat green turns).
#define INTERSECTION_GREEN_CENTER_FINISH_DEG              35.0f

// (U-turn detection + sequence removed — see git history.)

// =============================================================================
//  K230D AI processor
// =============================================================================
#define K230_BAUD           115200UL
#define K230_MAX_DETECTIONS 16
#define K230_CMD_INTERVAL   100        // ms

// K230 YOLO raw class IDs — MUST match the deployed model's class order.
// 3-class model: 0=dead (black ball), 1=alive (silver ball), 2=evac point.
// VERIFY against the model: watch the "K230D BOX cls=" serial output (PRINT_K230)
// and confirm a known object reports the expected id before relying on it.
#define K230_CLASS_DEAD     0   // black ball  — dead victim   (chase + grab)
#define K230_CLASS_ALIVE    1   // silver ball — live victim   (chase + grab)
#define K230_CLASS_POINT    2   // evacuation point / corner   (deposit target)

// Back-compat aliases for older code that referred to silver/black.
#define K230_CLASS_SILVER   K230_CLASS_ALIVE
#define K230_CLASS_BLACK    K230_CLASS_DEAD

#define K230_FRAME_WIDTH    640.0f
#define K230_FRAME_CENTER_X (K230_FRAME_WIDTH * 0.5f)

// points.kmodel class IDs — when the K230 is in POINTS mode, box cls = corner
// colour. Confirmed order: 0=green, 1=red.
#define K230_POINT_GREEN    0   // live-victim corner
#define K230_POINT_RED      1   // dead-victim corner

// colordet.kmodel class IDs — when the K230 runs the 7-colour line/field model,
// box cls = detected colour. Order MUST match the model's class list
// (datasets/colordet classes.txt: Black,Blue,Green,Orange,Red,Silver,Yellow).
// VERIFY against the model: watch "K230D BOX cls=/col=" (PRINT_K230) and confirm
// a known colour reports the expected id before relying on it.
#define K230_COLOR_BLACK    0
#define K230_COLOR_BLUE     1
#define K230_COLOR_GREEN    2
#define K230_COLOR_ORANGE   3
#define K230_COLOR_RED      4
#define K230_COLOR_SILVER   5
#define K230_COLOR_YELLOW   6
#define K230_COLOR_COUNT    7

// =============================================================================
//  Evacuation-zone mapping
// =============================================================================

// Map dimensions
#define EVAC_ZONE_W_MM      1200
#define EVAC_ZONE_H_MM       900
#define MAP_CELL_MM           30
#define MAP_DIM               40

// Robot geometry
#define WHEELBASE_MM        140.0f
#define ENTRANCE_X_MM         0.0f
#define ENTRANCE_Y_MM         0.0f

// ToF (VL53L7CX) hardware map — bus, XSHUT pins, I2C addresses, mount geometry,
// and array config — now lives in pins_teensy.h (TOF_* / TOFn_* defines).

// EKF noise
#define EKF_Q_V_FRAC          0.15f
#define EKF_Q_OMEGA           0.20f
#define EKF_Q_THETA_RAD       0.05f
#define EKF_R_YAW_RAD2        0.0012f
#define EKF_R_WALL_MM2      100.0f
