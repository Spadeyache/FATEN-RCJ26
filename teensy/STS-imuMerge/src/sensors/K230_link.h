#pragma once

// =============================================================================
//  Sensors::K230_link — raw byte transport for the K230D AI processor.
//
//  - Pulls bytes from Serial8 into a FIFO; consumer drains via readByte().
//  - Sends a 1-byte command (0x00 IDLE / 0x01 DETECT) every K230_CMD_INTERVAL.
//  - Frame parsing (header / count / payload / checksum) lives in
//    processing/K230Decode — this layer does NOT understand framing.
// =============================================================================

#include <Arduino.h>

namespace Sensors {
namespace K230_link {

void init();
void tick();              // services TX command + buffers RX bytes

// Returns next available byte, or -1 if buffer empty.
int  readByte();

// Set the command byte that tick() will keep sending. 0x00 idle, 0x01 detect.
void setCommand(uint8_t cmd);

}  // namespace K230_link
}  // namespace Sensors
