#pragma once

// =============================================================================
//  Processing::K230Decode â€” frame parser for K230D AI processor output.
//
//  Wire format from the K230D:  [0xAA][COUNT]([TYPE][X][Y]) Ã— COUNT [CHKSUM]
//      CHKSUM = XOR of (COUNT + all TYPE/X/Y bytes)
//
//  Pulls bytes from Sensors::K230_link and exposes the parsed detections.
//  Also owns the running-mode flag that tells K230_link whether to send
//  0x00 (idle) or 0x01 (detect).
// =============================================================================

#include <stdint.h>
#include "../../config.h"

namespace Processing {
namespace K230Decode {

enum ObjectType : uint8_t {
    NONE        = 0,
    ALIVE       = 1,
    DEAD        = 2,
    EVAC_RED    = 3,
    EVAC_GREEN  = 4,
    OBSTACLE    = 5,
};

struct Detection {
    ObjectType type;
    uint8_t    x;   // 0..255 (left=0, right=255)
    uint8_t    y;   // 0..255 (top=0,  bottom=255)
};

void tick();

const Detection* detections();   // pointer to internal array
uint8_t          count();         // number of valid entries

void setRunning(bool run);        // true â†’ send DETECT command, false â†’ IDLE
bool isRunning();

}  // namespace K230Decode
}  // namespace Processing
