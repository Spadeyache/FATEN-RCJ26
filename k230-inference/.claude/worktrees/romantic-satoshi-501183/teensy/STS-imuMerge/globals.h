#pragma once

#include "config.h"
#include "CommandFilter.h"
#include "YacheEncodedSerial.h"
#include "yacheSTS.h"
#include "IcsHardSerialClass.h"
#include <Servo.h>
#include <arm_math.h>

// =============================================================================
//  globals.h — Every cross-file global in one place
//
//  Include this in EVERY .ino file.  Variables are DEFINED in their logical
//  .ino file (noted in comments); this header only declares them as extern so
//  IntelliSense and the compiler can see them everywhere.
// =============================================================================

// --- State machine (STS-imuMerge.ino) ---
enum RobotState {
    FOLLOWING_LINE,  // Normal PID line follow
    EXECUTING_TURN,  // Mid-turn, ignoring new commands
    EVACUATION_ZONE, // Silver tape — stopped, periodic beep
    STALLED_RED      // Red tape — stopped until XIAO clears
};
extern RobotState robotState;
extern bool       isBusyTurning;  // True while a turn is running; blocks new commands
extern bool       disableGreen;   // True during cooldown after a green turn; prevents re-trigger

// --- XIAO comms (Comms.ino) ---
extern YacheEncodedSerial xiao;
extern uint8_t            xiaoCommand;   // Feature ID received on XIAO_REG_FEATURE (see XIAO_FEAT_*)
extern float              xiaoLineError; // Line COM 0-254 (127 = centre), XIAO_REG_COM
extern float              xiaoGapAngle;  // Gap angle 0-254 (127 = 0°), XIAO_REG_ANGLE
extern CommandFilter      cmdFilter;

// --- IMU (Sensors.ino) ---
extern volatile float32_t pitch; // Positive = nose-up
extern volatile float32_t roll;
extern volatile float32_t yaw;
extern volatile bool touchfront;
extern volatile bool conduct0;
extern volatile bool conduct1;



// --- Motors (Drive.ino) ---
extern yacheSTS            _sts;
extern IntervalTimer        controlTimer;
extern volatile float32_t  frontLeftGain;
extern volatile float32_t  frontRightGain;
extern volatile float32_t  backLeftGain;
extern volatile float32_t  backRightGain;

// --- Cross-file function declarations ---
// Declared here so every .ino that includes globals.h can call them
// without relying on Arduino's auto-prototype generator.
void motor(float32_t left, float32_t right); // Drive.ino
void updateSensors();                         // Sensors.ino

// --- Arm / Servos (Actions.ino) ---
extern Servo             _HS45HB0;
extern Servo             _HS45HB1;
extern IcsHardSerialClass krs;

// --- K230D AI detections (K230.ino) ---
enum K230ObjectType : uint8_t {
    K230_NONE        = 0,
    K230_ALIVE       = 1,  // Live victim
    K230_DEAD        = 2,  // Dead victim
    K230_EVAC_RED    = 3,  // Red evacuation point
    K230_EVAC_GREEN  = 4,  // Green evacuation point
    K230_OBSTACLE    = 5,  // Generic obstacle
};

struct Detection {
    K230ObjectType type;
    uint8_t        x;  // 0-255 normalized (0=left, 255=right)
    uint8_t        y;  // 0-255 normalized (0=top,  255=bottom)
};

extern Detection detections[K230_MAX_DETECTIONS];
extern uint8_t   detectionCount;  // Number of valid entries in detections[]
extern bool      k230Running;     // True while DETECT command is being sent

// --- Evacuation-zone mapping (Mapping.ino) ---
struct Pose;          // Pose.h
struct EvacMap;       // MapGrid.h
class  yacheVL53L7CX; // yacheVL53L7CX.h
extern Pose          g_pose;
extern EvacMap       g_map;
extern yacheVL53L7CX g_tof[TOF_COUNT];
