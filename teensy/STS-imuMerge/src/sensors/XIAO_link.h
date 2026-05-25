#pragma once

// =============================================================================
//  Sensors::XIAO_link — raw transport for the XIAO ESP32 register protocol.
//  Owns one YacheEncodedSerial on Serial3. No interpretation of register IDs.
// =============================================================================

#include <Arduino.h>

namespace Sensors {
namespace XIAO_link {

void init();
void tick();                                  // drains RX buffer into the cache

uint8_t get(uint8_t reg);                     // latest cached value for reg
void    send(uint8_t reg, uint8_t value);     // [255, reg, value]

}  // namespace XIAO_link
}  // namespace Sensors
