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
#define PRINT_IMU        1   // pitch/roll/yaw at 10 Hz
#define PRINT_XIAO       1   // CommandFilter votes + xiaoCommand
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
//   reg 0x04 ANGLE    X→T  gap line angle (gap mode)
//   reg 0x05 FLAG     X→T  bit0 commit, bit1 tight-turn slow drive
#define XIAO_REG_FEATURE    0x01
#define XIAO_REG_COM        0x02
#define XIAO_REG_MODE       0x03
#define XIAO_REG_ANGLE      0x04
#define XIAO_REG_FLAG       0x05

#define XIAO_FLAG_COMMIT      0x01
#define XIAO_FLAG_TIGHT_SLOW  0x02

// FEATURE byte — LINE-follow events (the clean contract; keep in sync with XIAO):
#define FEAT_NONE           0
#define FEAT_UTURN          1   // XIAO GreenFilter-confirmed both-green
#define FEAT_RED            2   // raw red on the scan row (Teensy filters)
#define FEAT_SILVER         3   // raw silver on the scan row (Teensy filters)
#define FEAT_LINE_LOST      4   // raw "no line" on the scan row (Teensy filters)
#define FEAT_GREEN_LEFT     7   // XIAO GreenFilter-confirmed left turn  (hardcoded fwd+turn)
#define FEAT_GREEN_RIGHT    8   // XIAO GreenFilter-confirmed right turn (hardcoded fwd+turn)

// FEATURE byte — mode-scoped codes for SEARCH_LINE / NOGI modes (separate code space):
#define FEAT_SEARCH_LINE_SILVER 5   // SEARCH_LINE mode: silver tape
#define FEAT_SEARCH_LINE_BLACK  6   // SEARCH_LINE mode: black return line (LINE_Obstacle waits on this)

enum XiaoMode : uint8_t {
    XIAO_MODE_LINE        = 0,
    XIAO_MODE_SEARCH_LINE = 1,
    XIAO_MODE_NOGI        = 2,
    XIAO_MODE_LINE_ANGLE  = 3,   // line slope + crossing count during gap traversal
    XIAO_MODE_OBSTACLE    = 4,   // obstacle re-acquire: arc see-line flag + line tilt angle
    XIAO_MODE_SILVER_ALIGN= 5,   // evac entry: silver-tape tilt angle for perpendicular align
};

// =============================================================================
//  Motor
// =============================================================================
#define MAX_MOTOR_SPEED     100

// Line-follow PID gains, base speed (FRIC_SPEED_*), and smoothing alphas now
// live in src/actions/Drive.cpp, next to the gravAdj/rotAxisAdj/frictionCircAdj
// tuning constants.

// =============================================================================
//  IMU
// =============================================================================
#define CALIBRATE_IMU       0   // 1 = calibrate at boot + save to EEPROM; 0 = load saved EEPROM offsets
#define IMU_SAMPLE_RATE     200.0f
#define IMU_PITCH_GAIN      0.0f
#define IMU_EMA_ALPHA       0.25f

// MPU6050 calibration offsets — fallback / initial-guess values (from IMU-01).
// At boot they are overwritten by either the auto-calibration (CALIBRATE_IMU==1)
// or the values loaded from EEPROM (CALIBRATE_IMU==0).
#define IMU_AX_OFFSET       -4737
#define IMU_AY_OFFSET        -374
#define IMU_AZ_OFFSET         631
#define IMU_GX_OFFSET          19
#define IMU_GY_OFFSET          54
#define IMU_GZ_OFFSET           2

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

// EVAC search victim sweep
#define EVAC_SEARCH_SPIN_LEFT       -40.0f
#define EVAC_SEARCH_SPIN_RIGHT       40.0f
#define EVAC_GRAB_BASE_SPEED        45.0f
#define EVAC_GRAB_TURN_GAIN         35.0f
#define EVAC_GRAB_AVG_FRAMES          3
#define EVAC_GRAB_LOST_HOLD_FRAMES    3
#define EVAC_GRAB_STOP_HEIGHT_PX     120.0f
#define EVAC_GRAB_STOP_WINDOW          4
#define EVAC_GRAB_STOP_REQUIRED        3

// EVAC collection / deposit policy
#define EVAC_MAX_BALLS                 3          // stop collecting at this many
#define EVAC_SEARCH_TIMEOUT_MS         120000UL   // 2-min collection window (from search start)
#define EVAC_POINT_STOP_HEIGHT_PX      120.0f     // deploy: stop approaching the corner at this box height
#define EVAC_DEPLOY_TIMEOUT_MS         30000UL    // safety: give up hunting the corner after this

// =============================================================================
//  CommandFilter — moving-average vote thresholds (votes within the last
//  FILTER_QUEUE_SIZE frames needed to confirm each event)
// =============================================================================
#define FILTER_QUEUE_SIZE        15
#define FILTER_THRESHOLD_RED      5   // red line
#define FILTER_THRESHOLD_SILVER   4   // silver (evac entry)
#define FILTER_THRESHOLD_LINELOST  3   // sustained line loss → gap
#define FILTER_THRESHOLD_GREEN    4   // green left/right (matches main); u-turn = both sides build up

// After firing any green turn (u-turn / left / right), ignore all green for this
// long so the same intersection isn't re-read on the way out.
#define DISABLE_GREEN_MS         500

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
