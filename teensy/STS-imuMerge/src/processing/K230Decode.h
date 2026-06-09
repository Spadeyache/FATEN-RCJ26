#pragma once

// =============================================================================
//  Processing::K230Decode - frame parser for K230D AI processor output.
//
//  Wire format from the K230D:
//      [0xAA][0x55][COUNT]([CLS][SCORE][X1][Y1][X2][Y2]) x COUNT [CHKSUM]
//
//  Each box is 10 bytes:
//      cls, score, x1_hi, x1_lo, y1_hi, y1_lo, x2_hi, x2_lo, y2_hi, y2_lo
//
//  CHKSUM = XOR of every byte from 0xAA through the last box byte.
//
//  YacheK230D owns the Serial5 byte parser. This layer adapts it to expose both
//  raw boxes and the older center-point Detection view used by the existing
//  state-machine code. It also owns the running-mode flag that tells K230_link
//  whether to send 0x00 (idle) or 0x01 (detect).
// =============================================================================

#include <stdint.h>
#include "../../config.h"
#include "../../yacheK230D.h"

namespace Processing {
namespace K230Decode {

enum ObjectType : uint8_t {
    SILVER      = 0,
    BLACK       = 1,

    // Legacy names kept so older state code can still compile while the
    // K230 YOLO model now reports silver/black classes.
    ALIVE       = SILVER,
    DEAD        = BLACK,
    EVAC_RED    = 3,
    EVAC_GREEN  = 4,
    OBSTACLE    = 5,
    NONE        = 255,
};

struct Detection {
    ObjectType type;
    uint8_t    x;   // 0..255 (left=0, right=255)
    uint8_t    y;   // 0..255 (top=0,  bottom=255)
};

using Box = K230DBox;

void tick();

const Detection* detections();   // pointer to internal array
uint8_t          count();         // number of valid entries
const Box*       boxes();         // pointer to internal box array
uint8_t          boxCount();      // number of valid boxes
uint32_t         lastPacketMs();  // millis() timestamp of last valid frame

void setRunning(bool run);        // true -> send DETECT command, false -> IDLE
bool isRunning();

}  // namespace K230Decode
}  // namespace Processing
