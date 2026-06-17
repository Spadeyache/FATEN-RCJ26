#pragma once

// =============================================================================
//  pins_teensy.h — Teensy 4.1 HARDWARE MAP (single source of truth)
//
//  Everything that describes the *physical wiring* of the board lives here:
//  pin numbers, serial-port assignments, the I2C bus, the 74HCT126 EN pin,
//  and on-bus device IDs. Swapping to a new PCB should mean editing only this
//  file.
//
//  Serial/Wire aliases are macros that expand to the core HardwareSerial /
//  TwoWire objects, so e.g. `_sts.begin(STS_SERIAL)` == `_sts.begin(Serial5)`.
//
//  Tuning constants, calibration, protocol register IDs, baud rates, and
//  software classes/enums stay in config.h — NOT here.
// =============================================================================

// =============================================================================
//  74HCT126 half-duplex buffer enables — ONE per bus (separate chips/channels)
// =============================================================================
//   Each half-duplex smart-servo bus has its own 74HCT126 output-enable line.
//     STS_EN_PIN : enables the STS drive-bus buffer. Held HIGH at boot (the
//                  yacheSTS driver does not touch it — Drive/sketch sets it).
//     KRS_EN_PIN : enables/directs the KRS arm-bus buffer. Driven by
//                  IcsHardSerialClass (HIGH = transmit, LOW = receive).
#define STS_EN_PIN           9       // 74HCT126 OE for STS bus
#define KRS_EN_PIN           2       // 74HCT126 OE / direction for KRS bus

// =============================================================================
//  Drive motors — Feetech STS3032 smart servos (4WD)
// =============================================================================
//   Single half-duplex bus shared by all four servos (1 Mbps), via 74HCT126.
#define STS_SERIAL          Serial5  // buffer enable: STS_EN_PIN (see above)

//   On-bus servo IDs (FL, FR, BL, BR).
#define STS_ID_FL            4
#define STS_ID_FR            1
#define STS_ID_BL            2
#define STS_ID_BR            3

// =============================================================================
//  Lift arm — KRS3031 smart servo (half-duplex via 74HCT126 buffer)
// =============================================================================
#define KRS_SERIAL          Serial1  // direction/enable: KRS_EN_PIN (see above)
#define KRS_ID               1       // KRS servo on-bus ID

// =============================================================================
//  Grab arm — HS-45HB hobby servos (PWM)
// =============================================================================
#define HS45HB0_PIN          3
#define HS45HB1_PIN          4

// =============================================================================
//  Vision / comms links
// =============================================================================
#define XIAO_SERIAL         Serial3  // XIAO ESP32 line-vision link
#define K230_SERIAL         Serial8  // K230D AI processor

// =============================================================================
//  IMU — MPU6050
// =============================================================================
#define IMU_WIRE            Wire    // I2C1 on PCB   SCL1 = pin 17, SDA1 = pin 16

// =============================================================================
//  ToF — VL53L7CX 4-sensor array (on its own I2C bus)
// =============================================================================
//   Bus: primary I2C (Wire0). On Teensy the primary bus object is `Wire`
//   (SDA0 = pin 18, SCL0 = pin 19).
#define TOF_WIRE            Wire     // Wire0 / primary I2C bus

//   Array-wide config.
#define TOF_COUNT              4
#define TOF_RES                8        // 8x8 multizone
#define TOF_MAX_MM          1320
#define TOF_MIN_MM            20
#define TOF_FREQ_HZ           15
#define TOF_FOV_DEG         60.0f

//   Per-sensor config (index 0..3). Adjust to your physical mount.
//     XSHUT : Teensy pin that holds this sensor in reset at boot.
//             Use -1 for a sensor with NO XSHUT wired (always powered on).
//             Sensor 0 has no XSHUT, so it is configured FIRST and moved off the
//             default 0x52 before any XSHUT sensor is woken (avoids a collision).
//     DX/DY : sensor origin in the robot frame (mm); +x forward, +y left
//     YAW   : sensor facing in the robot frame (deg); 0=fwd, +90=left, -90=right
//     ADDR  : unique 8-bit I2C address assigned at boot (default chip addr 0x52)
#define TOF0_XSHUT_PIN   -1
#define TOF0_DX_MM       50.0f
#define TOF0_DY_MM       40.0f
#define TOF0_YAW_DEG     40.0f
#define TOF0_ADDR        0x54

#define TOF1_XSHUT_PIN    39
#define TOF1_DX_MM       50.0f
#define TOF1_DY_MM      -40.0f
#define TOF1_YAW_DEG    -40.0f
#define TOF1_ADDR        0x56

#define TOF2_XSHUT_PIN    40
#define TOF2_DX_MM       30.0f
#define TOF2_DY_MM       60.0f
#define TOF2_YAW_DEG     90.0f
#define TOF2_ADDR        0x58

#define TOF3_XSHUT_PIN    41
#define TOF3_DX_MM       30.0f
#define TOF3_DY_MM      -60.0f
#define TOF3_YAW_DEG    -90.0f
#define TOF3_ADDR        0x5A

// =============================================================================
//  Onboard LED
// =============================================================================
#define LED_PIN             13   // Teensy 4.1 built-in LED

// =============================================================================
//  Buzzer
// =============================================================================
#define BUZZER_PIN          37

// =============================================================================
//  Front touch + conductivity probes
// =============================================================================
#define TOUCH_FRONT_PIN     36
// #define TOUCH_SIDE_PIN     33
#define CONDUCT0_PIN         5
#define CONDUCT1_PIN         6
