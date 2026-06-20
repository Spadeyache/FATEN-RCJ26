#pragma once

// =============================================================================
//  Sensors::ToF - VL53L7CX array on TOF_WIRE.
//  Passthrough to yacheVL53L7CX driver. processing/Mapping consumes the frames.
// =============================================================================

#include <Arduino.h>
#include "../../config.h"
#include "../drivers/yacheVL53L7CX.h"

namespace Sensors {
namespace ToF {

void init();                                          // calls TOF_WIRE.begin()
yacheVL53L7CX& sensor(uint8_t i);                     // i < TOF_COUNT
uint8_t        count();

}  // namespace ToF
}  // namespace Sensors
