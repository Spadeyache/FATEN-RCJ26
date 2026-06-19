#pragma once

// =============================================================================
//  Sensors::ToF â€” VL53L7CX array on Wire1.
//  Passthrough to yacheVL53L7CX driver. processing/Mapping consumes the frames.
// =============================================================================

#include <Arduino.h>
#include "../../config.h"
#include "../drivers/yacheVL53L7CX.h"

namespace Sensors {
namespace ToF {

void init();                                          // calls Wire1.begin()
yacheVL53L7CX& sensor(uint8_t i);                     // i < TOF_COUNT
uint8_t        count();

}  // namespace ToF
}  // namespace Sensors
