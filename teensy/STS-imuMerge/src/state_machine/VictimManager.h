#pragma once

#include <Arduino.h>

namespace VictimManager {

// Sentinel returned by leftColor()/rightColor() when that gripper is empty.
// 255 is safe: colordet class ids are 0..6, so no real colour collides with it.
constexpr uint8_t COLOR_NONE = 255;

void    reset();            // empty both grippers
uint8_t count();            // number of grippers currently holding (0..2)
bool    full();             // both grippers holding

bool    acceptsType(uint8_t type);   // grabbable colour + a gripper is free
bool    tryGrab(uint8_t type);       // align → grab (left, then right) → confirm

// The colour captured in each gripper — the "left"/"right" globals the caller
// reads later. Returns the colordet class id (0..6) or COLOR_NONE if empty.
uint8_t leftColor();
uint8_t rightColor();

}  // namespace VictimManager
