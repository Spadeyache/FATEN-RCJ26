#pragma once

// =============================================================================
//  Sensors::ToF - VL53L7CX array on TOF_WIRE.
//  Passthrough to yacheVL53L7CX driver. processing/Mapping consumes the frames.
// =============================================================================

#include <Arduino.h>
#include "../../config.h"
#include "../drivers/yacheVL53L7CX.h"

extern int16_t tofFL[8][8];  // Front-left ToF distances in mm. -1 = invalid/no fresh trusted target.

namespace Sensors {
namespace ToF {

void init();                                          // calls TOF_WIRE.begin()
bool tick();                                          // updates tofFL when a fresh frame is ready
void printFL();                                       // debug print of tofFL
yacheVL53L7CX& sensor(uint8_t i);                     // i < TOF_COUNT
uint8_t        count();

}  // namespace ToF
}  // namespace Sensors
