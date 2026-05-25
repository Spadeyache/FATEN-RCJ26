#pragma once

// =============================================================================
//  pins_teensy.h — Teensy 4.1 pin assignments
//
//  Only pin numbers live here. Tuning constants live in config.h.
// =============================================================================

// --- Buzzer ---
#define BUZZER_PIN          37

// --- Grab-arm hobby servos (HS-45HB) ---
#define HS45HB0_PIN          3
#define HS45HB1_PIN          4

// --- KRS half-duplex serial direction pin ---
#define PIN_74HCT126_EN      2

// --- Front touch + conductivity probes ---
#define TOUCH_FRONT_PIN     36
#define CONDUCT0_PIN         5
#define CONDUCT1_PIN         6

// --- Hardware serial ports (documented; pin assignments are MCU-fixed) ---
//   Serial1 → KRS smart servo (lift arm) — via 74HCT126 half-duplex buffer
//   Serial2 → STS smart servos (drive motors)
//   Serial3 → XIAO ESP32 link
//   Serial5 → K230D AI processor

// --- I²C buses ---
//   Wire  → (free)
//   Wire1 → MPU6050 IMU + VL53L7CX ToF (SCL1=pin17, SDA1=pin16)
