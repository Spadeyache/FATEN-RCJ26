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
#define PRINT_XIAO       0   // CommandFilter votes + xiaoCommand
#define PRINT_K230       1   // K230 detections
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
#define KRS_ID              1
#define KRS_SPD             43       // 1-127

// =============================================================================
//  Motor + line-follow PID
// =============================================================================
#define MAX_MOTOR_SPEED     100

#define PID_KP              3.0f
#define PID_KI              0.0f
#define PID_KD              3.0f
#define PID_BASE_SPEED      100.0f
#define PID_LEFT_SCALE      1.7f
#define PID_INTEGRAL_LIMIT  500.0f
#define LINE_EMA_ALPHA      0.3f
#define DERIV_EMA_ALPHA     0.4f

// =============================================================================
//  IMU
// =============================================================================
#define CALIBRATE_IMU       0
#define IMU_SAMPLE_RATE     200.0f
#define IMU_PITCH_GAIN      0.0f
#define IMU_EMA_ALPHA       0.25f

// =============================================================================
//  Action: Turn — motor speeds + duration calibration
// =============================================================================
#define TURN_UTURN_L       -70.0f
#define TURN_UTURN_R        70.0f
#define TURN_LEFT_L        -70.0f
#define TURN_LEFT_R        100.0f
#define TURN_RIGHT_L       100.0f
#define TURN_RIGHT_R       -70.0f

#define TURN_UTURN_MS       3200
#define TURN_MS_PER_DEG     11.2

// =============================================================================
//  Action: Forward — distance/time calibration
// =============================================================================
#define FORWARD_MS_PER_MM   7.7f
#define FORWARD_YAW_KP      1.5f

// =============================================================================
//  State machine timings
// =============================================================================
#define DISABLE_GREEN_MS    750     // green-turn cooldown after firing one

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

// ToF (VL53L7CX)
#define TOF_COUNT              1
#define TOF_MAX_MM          1320
#define TOF_MIN_MM            20
#define TOF_RES                4
#define TOF_FREQ_HZ           15
#define TOF_FOV_DEG         60.0f
#define TOF_LPN_PIN_FRONT     -1
#define TOF_I2C_RST_PIN       -1
#define TOF_FRONT_DX_MM       0.0f
#define TOF_FRONT_DY_MM       0.0f
#define TOF_FRONT_YAW_RAD     0.0f

// Log-odds occupancy
#define LO_HIT                 6
#define LO_MISS               -2
#define LO_CLAMP              64
#define LO_DECISIVE           30

// EKF noise
#define EKF_Q_V_FRAC          0.15f
#define EKF_Q_OMEGA           0.20f
#define EKF_Q_THETA_RAD       0.05f
#define EKF_R_YAW_RAD2        0.0012f
#define EKF_R_WALL_MM2      100.0f

// Recalibration / stuck detection
#define RECAL_STUCK_MS         1000
#define RECAL_MIN_VCMD_MMPS      30
#define RECAL_DPOSE_MM            5
#define RECAL_WIN_XY_MM          60
#define RECAL_WIN_TH_DEG         10
#define RECAL_STEP_XY_MM         15
#define RECAL_STEP_TH_DEG         2

// EEPROM
#define EEPROM_MAP_BASE       0x0020
#define EEPROM_MAP_MAGIC      0xE7ACC001UL
#define EEPROM_MAP_VERSION       1
#define MAP_CHECKPOINT_MS    30000
#define MAP_CHECKPOINT_MIN_DIRTY 60
