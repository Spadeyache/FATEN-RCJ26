#pragma once

// =============================================================================
//  config.h â€” Tune everything from here
// =============================================================================

// --- Hardware Pins ---
#define BUZZER_PIN          37
#define HS45HB0_PIN         3      // arm servo A
#define HS45HB1_PIN         4      // arm servo B
#define PIN_74HCT126_EN     2      // Direction pin for KRS half-duplex serial
#define _touchfront 36
#define _conductPin0 5
#define _conductPin1 6


// --- Serial Baud Rates ---
#define XIAO_BAUD           4000000UL

// --- Xiao serial register IDs ---
// These match the IDs defined in the Xiao's config.h (shared protocol).
#define XIAO_REG_FEATURE    0x01   // Xiao → Teensy : detected feature
#define XIAO_REG_COM        0x02   // Xiao → Teensy : line centre-of-mass
#define XIAO_REG_MODE       0x03   // Teensy → Xiao : active mode
#define XIAO_REG_ANGLE      0x04   // Xiao → Teensy : gap line angle (mode 3)

// Xiao mode values (sent on XIAO_REG_MODE)
enum XiaoMode : uint8_t {
    XIAO_MODE_LINE = 0,   // Line follow (default)
    XIAO_MODE_EVAC = 1,   // Evac tape detection
    XIAO_MODE_NOGI = 2,   // No-green intersection detection
    XIAO_MODE_GAP  = 3,   // Gap / line-angle estimation
};

// // Xiao feature IDs (received on XIAO_REG_FEATURE)
// #define XIAO_FEAT_NONE           0
// #define XIAO_FEAT_UTURN          1
// #define XIAO_FEAT_GREEN_LEFT     2
// #define XIAO_FEAT_GREEN_RIGHT    3
// #define XIAO_FEAT_RED            4
// #define XIAO_FEAT_SILVER         5
// #define XIAO_FEAT_BLACK_INTERSECT 6
// #define XIAO_FEAT_EVAC_SILVER    7
// #define XIAO_FEAT_EVAC_BLACK     8
// #define XIAO_FEAT_NOGI_INTERSECT 9
#define KRS_BAUD            115200UL
#define KRS_TIMEOUT         400      // ms
#define KRS_ID              1

// --- Motor Speed Limits ---
#define MAX_MOTOR_SPEED     100      // Absolute cap applied to all wheel commands

// --- Line-follow PID (Teensy-side) ---
// Error is derived from xiaoLineError (0â€“254, centre = 127), scaled to Â±200
#define PID_KP              3.0f     // Proportional gain (lowered from 4.5 — smoother at 50 Hz)
#define PID_KI              0.0f     // Integral gain
#define PID_KD              3.0f     // Derivative gain (now acts on error/second, freq-independent)
#define PID_BASE_SPEED      100.0f   // Straight-ahead wheel speed
#define PID_LEFT_SCALE      1.7f     // Extra gain on left motor (compensates camera lighting bias)
#define PID_INTEGRAL_LIMIT  500.0f   // Anti-windup clamp on the I-term accumulator
#define LINE_EMA_ALPHA      0.3f     // Line error EMA smoothing (lower = smoother, less responsive)
#define DERIV_EMA_ALPHA     0.4f     // Derivative EMA smoothing (lower = smoother D-term)

// --- IMU ---
// Set CALIBRATE_IMU 1 to run calibration and print offsets to Serial,
// then hardcode the printed values in yacheMPU6050.h and set back to 0.
#define CALIBRATE_IMU       0
#define IMU_SAMPLE_RATE     200.0f   // Hz for Madgwick filter (unused; DMP runs at 100 Hz)
#define IMU_PITCH_GAIN      0.0f
#define IMU_EMA_ALPHA       0.25f    // EMA smoothing: lower = smoother but slower (0.1–0.5)

// --- Turn Motor Speeds ---
#define TURN_UTURN_L       -70.0f
#define TURN_UTURN_R        70.0f
#define TURN_LEFT_L        -70.0f
#define TURN_LEFT_R        100.0f
#define TURN_RIGHT_L       100.0f
#define TURN_RIGHT_R       -70.0f

// --- Turn Durations (ms) ---
#define TURN_UTURN_MS       3200
#define TURN_MS_PER_DEG        11.2

// --- execForward distance calibration ---
// At MAX_MOTOR_SPEED the robot travels ~1 mm every FORWARD_MS_PER_MM ms.
// Measure: drive at speed 100 for 1000 ms, measure distance in mm, then
// set FORWARD_MS_PER_MM = 1000 / distance_mm.
#define FORWARD_MS_PER_MM   7.7f    // ms per mm at MAX_MOTOR_SPEED     calibrate!
#define FORWARD_YAW_KP      1.5f    // P gain for IMU yaw-hold straight driving

// --- Green marker cooldown ---
// After a left/right turn fires, green commands are ignored for this long.
// Prevents re-triggering the same intersection if the motion was too short.
#define DISABLE_GREEN_MS    750     // ms -- tune to be longer than your turn duration

// --- CommandFilter ---
#define FILTER_QUEUE_SIZE   15       // Rolling window length
#define FILTER_THRESHOLD    4        // used to be 7 : Votes needed to confirm a command
#define FILTER_THRESHOLD_RED 5
#define FILTER_THRESHOLD_SILVER 4
#define FILTER_THRESHOLD_INTERSECTION 4

// --- Evacuation Zone ---
// #define EVAC_BEEP_ON_MS     20       // Buzzer on-time per beep
// #define EVAC_BEEP_PERIOD_MS 500      // Time between beep starts

// --- K230D AI Processor (K230.ino) ---
#define K230_BAUD           115200UL
#define K230_MAX_DETECTIONS 8          // Max objects per frame
#define K230_CMD_INTERVAL   100        // ms between Teensy→K230 command sends

// =============================================================================
//  Evacuation-zone mapping (Mapping.ino + MapGrid + Pose + ToF)
// =============================================================================

// --- Map ---
#define EVAC_ZONE_W_MM      1200
#define EVAC_ZONE_H_MM       900
#define MAP_CELL_MM           30
#define MAP_DIM               40        // 40x40 cells (handles either entrance-wall orientation)

// --- Robot geometry ---
#define WHEELBASE_MM        140.0f      // CALIBRATE — distance between left and right wheels
#define ENTRANCE_X_MM         0.0f      // robot starts at robot-frame origin
#define ENTRANCE_Y_MM         0.0f

// --- ToF (VL53L7CX) ---
#define TOF_COUNT              1        // Phase 1: front only. Driver array-ready for 4.
#define TOF_MAX_MM          1320        // 120 cm * 1.1 safety margin
#define TOF_MIN_MM            20
#define TOF_RES                4        // zones per side (4 → 16 zones, 8 → 64)
#define TOF_FREQ_HZ           15        // 4x4 supports 1..60 Hz; 8x8 supports 1..15
#define TOF_FOV_DEG         60.0f
#define TOF_LPN_PIN_FRONT     -1        // -1 = use default I2C addr (no LPn juggling)
#define TOF_I2C_RST_PIN       -1
// Front sensor mount (sensor-frame origin relative to robot center, sensor +x = forward):
#define TOF_FRONT_DX_MM       0.0f
#define TOF_FRONT_DY_MM       0.0f
#define TOF_FRONT_YAW_RAD     0.0f

// --- Log-odds occupancy ---
#define LO_HIT                 6
#define LO_MISS               -2
#define LO_CLAMP              64
#define LO_DECISIVE           30

// --- EKF noise ---
#define EKF_Q_V_FRAC          0.15f     // process std as fraction of commanded v
#define EKF_Q_OMEGA           0.20f     // process std on omega (rad/s)
#define EKF_Q_THETA_RAD       0.05f     // process std on theta drift per tick
#define EKF_R_YAW_RAD2        0.0012f   // ≈ (2°)^2 in rad^2
#define EKF_R_WALL_MM2      100.0f      // (10 mm)^2

// --- Recalibration ---
#define RECAL_STUCK_MS         1000
#define RECAL_MIN_VCMD_MMPS      30
#define RECAL_DPOSE_MM            5
#define RECAL_WIN_XY_MM          60
#define RECAL_WIN_TH_DEG         10
#define RECAL_STEP_XY_MM         15
#define RECAL_STEP_TH_DEG         2

// --- EEPROM ---
#define EEPROM_MAP_BASE       0x0020    // after IMU calib block at 0x0000-0x001B
#define EEPROM_MAP_MAGIC      0xE7ACC001UL
#define EEPROM_MAP_VERSION       1
#define MAP_CHECKPOINT_MS    30000      // periodic checkpoint cadence
#define MAP_CHECKPOINT_MIN_DIRTY 60     // skip checkpoint unless ≥N cells changed
