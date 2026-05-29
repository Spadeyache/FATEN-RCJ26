#pragma once

// =============================================================================
//  WeightDistribution — IMU-tilt-based per-wheel gain tables.
//
//  Used by Actions::Drive::motor() when imuCompensation = true, to scale
//  motor power based on robot tilt (pitch / roll) reported by Sensors::IMU.
//
//  Layout (both arrays):
//    36 cells, signed angle from -35° to +35° in 2° steps (no 0° entry).
//      index 0  → -35°
//      index 1  → -33°
//      ...
//      index 17 → -1°
//      index 18 → +1°
//      ...
//      index 35 → +35°
//
//    To look up: idx = (angleDeg + 35) / 2   (clamp angleDeg to [-35, +35]).
//
//  Values are uint8_t 0..255; divide by 255.0f to get a 0.0..1.0 multiplier.
//  255 = full power (1.0), 0 = motor cut (0.0).
//
//  pitchDistribution → applied to front vs back wheels (positive pitch = nose up).
//  rollDistribution  → applied to left vs right wheels (positive roll  = right side down).
//
//  Defaults are all 255 (neutral / no compensation) — tune as needed.
// =============================================================================

#include <stdint.h>

namespace Actions {
namespace Drive {

// Pitch: aggressive profile (currently the active axis). Tight 3° deadband,
// ~14% cut by 10°, ~32% by 20°, up to ~57% cut at ±35°.
//   pitch > 0 (nose up)   → cuts FL, FR (front lifted)
//   pitch < 0 (nose down) → cuts BL, BR (back  lifted)
constexpr uint8_t pitchDistribution[36] = {
    110, 118, 126, 134, 142, 150, 158, 166, 175,   //  -35°..-19°
    185, 195, 205, 215, 225, 235, 245, 255, 255,   //  -17°..  -1°
    255, 255, 245, 235, 225, 215, 205, 195, 185,   //   +1°..+17°
    175, 166, 158, 150, 142, 134, 126, 118, 110,   //  +19°..+35°
};

// Roll: gentle profile (currently disabled; kept for when it's re-enabled).
//   roll > 0 (right down) → cuts FL, BL (left lifted)
//   roll < 0 (left  down) → cuts FR, BR (right lifted)
constexpr uint8_t rollDistribution[36] = {
    195, 200, 205, 210, 215, 220, 224, 228, 232,   //  -35°..-19°
    236, 240, 243, 246, 249, 252, 255, 255, 255,   //  -17°..  -1°
    255, 255, 255, 252, 249, 246, 243, 240, 236,   //   +1°..+17°
    232, 228, 224, 220, 215, 210, 205, 200, 195,   //  +19°..+35°
};

}  // namespace Drive
}  // namespace Actions
