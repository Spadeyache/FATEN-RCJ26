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
//   reg 0x04 ANGLE    X→T  gap line angle (gap mode)
//   reg 0x05 FLAG     X→T  bit0 commit, bit1 tight-turn slow drive
#define XIAO_REG_FEATURE    0x01
#define XIAO_REG_COM        0x02
#define XIAO_REG_MODE       0x03
#define XIAO_REG_ANGLE      0x04
#define XIAO_REG_FLAG       0x05
#define XIAO_REG_FINE_ANGLE 0x06

#define XIAO_FLAG_COMMIT      0x01
#define XIAO_FLAG_TIGHT_SLOW  0x02
#define XIAO_FLAG_BOTTOM_LINE 0x04
#define XIAO_FLAG_FINE_ANGLE  0x08

// FEATURE byte — LINE-follow events (the clean contract; keep in sync with XIAO):
#define FEAT_NONE           0
#define FEAT_UTURN          1   // XIAO GreenFilter-confirmed both-green
#define FEAT_RED            2   // raw red on the scan row (Teensy filters)
#define FEAT_SILVER         3   // raw silver on the scan row (Teensy filters)
#define FEAT_LINE_LOST      4   // raw "no line" on the scan row (Teensy filters)
#define FEAT_BLACK_INTERSECT 6   // saturated black row in LINE mode; suppress green rereads
#define FEAT_GREEN_LEFT     7   // XIAO GreenFilter-confirmed left turn  (hardcoded fwd+turn)
#define FEAT_GREEN_RIGHT    8   // XIAO GreenFilter-confirmed right turn (hardcoded fwd+turn)

// FEATURE byte — mode-scoped codes for SEARCH_LINE / NOGI modes (separate code space):
#define FEAT_SEARCH_LINE_SILVER 5   // SEARCH_LINE mode: silver tape
#define FEAT_SEARCH_LINE_BLACK  6   // SEARCH_LINE mode: black return line (LINE_Obstacle waits on this)

enum XiaoMode : uint8_t {
    XIAO_MODE_LINE        = 0,
    XIAO_MODE_SEARCH_LINE = 1,
    XIAO_MODE_NOGI        = 2,
    XIAO_MODE_LINE_ANGLE  = 3,   // line slope + point flags/Y during gap traversal
    XIAO_MODE_OBSTACLE    = 4,   // obstacle re-acquire: arc see-line flag + line tilt angle
    XIAO_MODE_EVAC_COLOR_MASK = 5,   // evac entry/exit: silver side scan + row-45 black flag
};

// =============================================================================
//  Motor
// =============================================================================
#define MAX_MOTOR_SPEED     100

// Shared line-follow base speeds. Drive.cpp uses these for PID base speed;
// LINE_Follow.cpp intersection forward moves use the same values by state.
#define LINE_FOLLOW_BASE_SPEED_FLAT   70.0f
#define LINE_FOLLOW_BASE_SPEED_SLOPE  45.0f

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

#define IMU_AX_OFFSET       -897
#define IMU_AY_OFFSET        -1901
#define IMU_AZ_OFFSET         1060
#define IMU_GX_OFFSET          203
#define IMU_GY_OFFSET          -49
#define IMU_GZ_OFFSET           -59

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

// EVAC shared tuning — used by BOTH EVAC_SearchDeploy (approach stop) AND
// VictimManager (grab self-confirm), so it stays here.
#define EVAC_GRAB_STOP_HEIGHT_PX     123.0f
// Other EVAC tuning is now file-local: EVAC_SearchDeploy-only constants live in
// EVAC_SearchDeploy.cpp; EVAC_MAX_BALLS lives in VictimManager.cpp.

// =============================================================================
//  CommandFilter — moving-average vote thresholds (votes within the last
//  FILTER_QUEUE_SIZE frames needed to confirm each event)
// =============================================================================
#define FILTER_QUEUE_SIZE        15
#define FILTER_THRESHOLD_RED      5   // red line
#define FILTER_THRESHOLD_SILVER   4   // silver (evac entry)
#define FILTER_THRESHOLD_LINELOST  3   // sustained line loss → gap
#define FILTER_THRESHOLD_BLACK_INTERSECT 8  // saturated black row before/through an intersection
#define FILTER_THRESHOLD_GREEN    8   // green left/right (matches main); u-turn = both sides build up

// After firing any green turn (u-turn / left / right), ignore all green for this
// long so the same intersection isn't re-read on the way out.
#define DISABLE_GREEN_MS         1000
#define BLACK_INTERSECT_DISABLE_GREEN_BASE_MS 1100
#define BLACK_INTERSECT_DISABLE_GREEN_MIN_MS   700
#define BLACK_INTERSECT_DISABLE_GREEN_MAX_MS  2000

// =============================================================================
//  Intersection / green-marker action tuning
//
//  Drive::lineFollowState() selects one row:
//    FLAT, NOSE_UP, NOSE_DOWN, LEFT_DOWN, RIGHT_DOWN
//
//  Forward distance/speed runs before the turn. U-turn forward distance is 0 by
//  default, so the current behavior is unchanged unless you tune it up.
// =============================================================================

#define INTERSECTION_GREEN_LEFT_FLAT_FORWARD_SPEED        LINE_FOLLOW_BASE_SPEED_FLAT
#define INTERSECTION_GREEN_LEFT_FLAT_FORWARD_MM           52.0f
#define INTERSECTION_GREEN_LEFT_FLAT_TURN_ANGLE          -90.0f
#define INTERSECTION_GREEN_LEFT_FLAT_TURN_SPEED           60.0f

// Nose-up / nose-down turns are symmetric: left and right share the same
// forward values and turn speed; only the turn angle sign is mirrored.
#define INTERSECTION_GREEN_NOSE_UP_FORWARD_SPEED          LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_GREEN_NOSE_UP_FORWARD_MM             115.0f
#define INTERSECTION_GREEN_NOSE_UP_TURN_ANGLE             68.0f
#define INTERSECTION_GREEN_NOSE_UP_TURN_SPEED             45.0f

#define INTERSECTION_GREEN_NOSE_DOWN_FORWARD_SPEED        -LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_GREEN_NOSE_DOWN_FORWARD_MM           32.0f
#define INTERSECTION_GREEN_NOSE_DOWN_TURN_ANGLE           90.0f
#define INTERSECTION_GREEN_NOSE_DOWN_TURN_SPEED           45.0f

// Side-down green turns are diagonal pairs:
//   green-left on left-down  == green-right on right-down, with angle sign mirrored.
//   green-left on right-down == green-right on left-down,  with angle sign mirrored.
#define INTERSECTION_GREEN_SIDE_DOWNHILL_FORWARD_SPEED    LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_GREEN_SIDE_DOWNHILL_FORWARD_MM       69.0f
#define INTERSECTION_GREEN_SIDE_DOWNHILL_TURN_ANGLE       90.0f
#define INTERSECTION_GREEN_SIDE_DOWNHILL_TURN_SPEED       45.0f

#define INTERSECTION_GREEN_SIDE_UPHILL_FORWARD_SPEED      INTERSECTION_GREEN_SIDE_DOWNHILL_FORWARD_SPEED
#define INTERSECTION_GREEN_SIDE_UPHILL_FORWARD_MM         INTERSECTION_GREEN_SIDE_DOWNHILL_FORWARD_MM
#define INTERSECTION_GREEN_SIDE_UPHILL_TURN_ANGLE         80.0f
#define INTERSECTION_GREEN_SIDE_UPHILL_TURN_SPEED         INTERSECTION_GREEN_SIDE_DOWNHILL_TURN_SPEED
//add go a small turn before foward at start? and less turn at the end

#define INTERSECTION_GREEN_RIGHT_FLAT_FORWARD_SPEED       LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_GREEN_RIGHT_FLAT_FORWARD_MM          52.0f
#define INTERSECTION_GREEN_RIGHT_FLAT_TURN_ANGLE          90.0f
#define INTERSECTION_GREEN_RIGHT_FLAT_TURN_SPEED          60.0f


// U-turn sequence:
//   pre-forward -> first turn -> mid-forward -> final turn -> search-line align.
// Speeds may be signed; negative forward speed drives backward for that segment.
#define INTERSECTION_UTURN_FLAT_PRE_FORWARD_SPEED         60.0f
#define INTERSECTION_UTURN_FLAT_PRE_FORWARD_MM            50.0f
#define INTERSECTION_UTURN_FLAT_FIRST_TURN_ANGLE          90.0f
#define INTERSECTION_UTURN_FLAT_FIRST_TURN_SPEED          60.0f
#define INTERSECTION_UTURN_FLAT_MID_FORWARD_SPEED         LINE_FOLLOW_BASE_SPEED_FLAT
#define INTERSECTION_UTURN_FLAT_MID_FORWARD_MM             0.0f
#define INTERSECTION_UTURN_FLAT_FINAL_TURN_ANGLE          90.0f
#define INTERSECTION_UTURN_FLAT_FINAL_TURN_SPEED          60.0f

#define INTERSECTION_UTURN_NOSE_UP_PRE_FORWARD_SPEED      LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_UTURN_NOSE_UP_PRE_FORWARD_MM         77.0f
#define INTERSECTION_UTURN_NOSE_UP_FIRST_TURN_ANGLE       90.0f
#define INTERSECTION_UTURN_NOSE_UP_FIRST_TURN_SPEED       45.0f
#define INTERSECTION_UTURN_NOSE_UP_MID_FORWARD_SPEED      -LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_UTURN_NOSE_UP_MID_FORWARD_MM         40.0f
#define INTERSECTION_UTURN_NOSE_UP_FINAL_TURN_ANGLE       90.0f
#define INTERSECTION_UTURN_NOSE_UP_FINAL_TURN_SPEED       45.0f

#define INTERSECTION_UTURN_NOSE_DOWN_PRE_FORWARD_SPEED    -LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_UTURN_NOSE_DOWN_PRE_FORWARD_MM       50.0f
#define INTERSECTION_UTURN_NOSE_DOWN_FIRST_TURN_ANGLE     90.0f
#define INTERSECTION_UTURN_NOSE_DOWN_FIRST_TURN_SPEED     45.0f
#define INTERSECTION_UTURN_NOSE_DOWN_MID_FORWARD_SPEED    -LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_UTURN_NOSE_DOWN_MID_FORWARD_MM       40.0f
#define INTERSECTION_UTURN_NOSE_DOWN_FINAL_TURN_ANGLE     90.0f
#define INTERSECTION_UTURN_NOSE_DOWN_FINAL_TURN_SPEED     45.0f

#define INTERSECTION_UTURN_LEFT_DOWN_PRE_FORWARD_SPEED    LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_UTURN_LEFT_DOWN_PRE_FORWARD_MM       50.0f
#define INTERSECTION_UTURN_LEFT_DOWN_FIRST_TURN_ANGLE     75.0f
#define INTERSECTION_UTURN_LEFT_DOWN_FIRST_TURN_SPEED     45.0f
#define INTERSECTION_UTURN_LEFT_DOWN_MID_FORWARD_SPEED    LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_UTURN_LEFT_DOWN_MID_FORWARD_MM       82.0f
#define INTERSECTION_UTURN_LEFT_DOWN_FINAL_TURN_ANGLE     65.0f
#define INTERSECTION_UTURN_LEFT_DOWN_FINAL_TURN_SPEED     45.0f

#define INTERSECTION_UTURN_RIGHT_DOWN_PRE_FORWARD_SPEED   LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_UTURN_RIGHT_DOWN_PRE_FORWARD_MM      50.0f
#define INTERSECTION_UTURN_RIGHT_DOWN_FIRST_TURN_ANGLE    75.0f
#define INTERSECTION_UTURN_RIGHT_DOWN_FIRST_TURN_SPEED    45.0f
#define INTERSECTION_UTURN_RIGHT_DOWN_MID_FORWARD_SPEED  -LINE_FOLLOW_BASE_SPEED_SLOPE
#define INTERSECTION_UTURN_RIGHT_DOWN_MID_FORWARD_MM      82.0f
#define INTERSECTION_UTURN_RIGHT_DOWN_FINAL_TURN_ANGLE    65.0f
#define INTERSECTION_UTURN_RIGHT_DOWN_FINAL_TURN_SPEED    45.0f

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
