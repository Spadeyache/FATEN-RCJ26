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

// XIAO register IDs — shared protocol with xiaoesp32/. Keep in sync.
#define XIAO_REG_FEATURE    0x01   // Xiao → Teensy : detected feature
#define XIAO_REG_COM        0x02   // Xiao → Teensy : line centre-of-mass
#define XIAO_REG_MODE       0x03   // Teensy → Xiao : active mode
#define XIAO_REG_ANGLE      0x04   // Xiao → Teensy : gap line angle (mode 3)

enum XiaoMode : uint8_t {
    XIAO_MODE_LINE = 0,
    XIAO_MODE_EVAC = 1,
    XIAO_MODE_NOGI = 2,
    XIAO_MODE_GAP  = 3,
};

#define KRS_BAUD            115200UL
#define KRS_TIMEOUT         400      // ms
#define KRS_SPD             43       // 1-127
//   KRS_ID and serial port live in pins_teensy.h (hardware map)

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
#define DISABLE_GREEN_MS    750     // green-turn cooldown after firing one

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

// =============================================================================
//  CommandFilter — vote thresholds
// =============================================================================
#define FILTER_QUEUE_SIZE             15
#define FILTER_THRESHOLD               4   // green left/right
#define FILTER_THRESHOLD_RED           5
#define FILTER_THRESHOLD_SILVER        4
#define FILTER_THRESHOLD_INTERSECTION  6   // no-green intersection (NGI)
#define FILTER_THRESHOLD_NOLINE        3   // sustained line loss

// =============================================================================
//  K230D AI processor
// =============================================================================
#define K230_BAUD           115200UL
#define K230_MAX_DETECTIONS 16
#define K230_CMD_INTERVAL   100        // ms

// K230 YOLO class IDs. Set these to the model's raw output IDs.
// Current model/viewer mapping is flipped, so raw 1=silver and raw 0=black.
#define K230_CLASS_SILVER   1
#define K230_CLASS_BLACK    0
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
