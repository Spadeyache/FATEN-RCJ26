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
//  YacheK230D owns the Serial8 byte parser. This layer adapts it to expose both
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
    DEAD        = K230_CLASS_DEAD,    // black ball
    ALIVE       = K230_CLASS_ALIVE,   // silver ball
    POINT       = K230_CLASS_POINT,   // evacuation point / corner

    // Back-compat aliases.
    BLACK       = DEAD,
    SILVER      = ALIVE,
    NONE        = 255,
};

// colordet.kmodel classes — box cls when the K230 runs the 7-colour line/field
// model. Values MUST match the K230_COLOR_* ids in config.h (and the K230-side
// colordet_labels.txt order). Distinct from ObjectType above, which describes
// the victim-ball model used in the EVAC phase.
enum ColorClass : uint8_t {
    COLOR_BLACK   = K230_COLOR_BLACK,
    COLOR_BLUE    = K230_COLOR_BLUE,
    COLOR_GREEN   = K230_COLOR_GREEN,
    COLOR_ORANGE  = K230_COLOR_ORANGE,
    COLOR_RED     = K230_COLOR_RED,
    COLOR_SILVER  = K230_COLOR_SILVER,
    COLOR_YELLOW  = K230_COLOR_YELLOW,
    COLOR_NONE    = 255,
};

// Human-readable name for a colordet class id ("Black".."Yellow"), or "?" if
// the id is out of range. Handy for PRINT_K230 debug output.
const char* colorName(uint8_t cls);

struct Detection {
    ObjectType type;
    uint8_t    x;   // 0..255 (left=0, right=255)
    uint8_t    y;   // 0..255 (top=0,  bottom=255)
};

using Box = K230DBox;

// Current victim (Dead/Alive ball) target.
namespace goalPOS {
extern bool valid;
extern float direction;   // normalized: (box_center_x - 320) / 320
extern uint8_t cls;       // K230_CLASS_DEAD or K230_CLASS_ALIVE
extern uint8_t score;
extern uint32_t updatedMs;
}

// Current evacuation-point (corner) target.
namespace pointPOS {
extern bool valid;
extern float direction;   // normalized: (box_center_x - 320) / 320
extern uint8_t score;
extern uint32_t updatedMs;
}

void tick();

const Detection* detections();   // pointer to internal array
uint8_t          count();         // number of valid entries
const Box*       boxes();         // pointer to internal box array
uint8_t          boxCount();      // number of valid boxes
uint32_t         lastPacketMs();  // millis() timestamp of last valid frame

bool isVictimClass(uint8_t cls);   // true for Dead or Alive ball classes
bool isPointClass(uint8_t cls);    // true for the evac-point/corner class
bool updateGoalFromVictim(const K230DBox &msg);

// Check one decoded K230D box. Returns true only for configured silver/black
// victim classes, and updates goalPOS from this box when it matches.
bool checkVictim(const K230DBox &msg);

// Check a caller-provided list of decoded K230D boxes. Picks the highest-score
// silver/black match, updates goalPOS from it, and returns true. If there is no
// match, clears goalPOS::valid and returns false.
bool checkVictim(const K230DBox *msgs, uint8_t msgCount);

// Check the most recent K230D frame already stored by this decoder. This is the
// normal call for state-machine code such as EVAC.
bool checkVictim();

// Evac-point (corner) detection, mirroring the victim helpers above. Picks the
// highest-score POINT box, updates pointPOS, and returns true. With no match it
// clears pointPOS::valid and returns false. Used by the deposit flow.
bool checkPoint(const K230DBox *msgs, uint8_t msgCount);
bool checkPoint();

// Returns the center X pixel (0..639) of the largest (by box area) detection
// of the given class in the most recent frame. Returns -1 if no box of that
// class is present.
int16_t largestCenterX(uint8_t cls);

// Returns the tallest box height (y2-y1) among detections of the given class in
// the most recent frame, or -1 if none. Used to confirm a grab succeeded (a
// same-type ball still tall in view = still on the floor = grab failed).
int16_t largestHeight(uint8_t cls);

// Returns the class id of the largest-area box in the most recent frame (any
// class), or -1 if none. In POINTS mode this is the corner colour.
int16_t dominantClass();

// Which model the K230 should run. Sends the matching command byte.
enum Model : uint8_t { MODEL_VICTIMS, MODEL_POINTS };
void setModel(Model m);

// Block for `ms` while continuously draining the K230 RX buffer, so it never
// overflows and the next read sees the newest frame. Use instead of delay()
// in any blocking sequence that runs while the K230 is streaming.
void drainDelay(uint32_t ms);

// Drain RX until a complete frame newer than `sincePacketMs` arrives.
// Returns false on timeout, leaving the most recent decoded frame in place.
bool waitForFreshFrameAfter(uint32_t sincePacketMs, uint32_t timeoutMs);

void setRunning(bool run);        // true -> send DETECT command, false -> IDLE
bool isRunning();

}  // namespace K230Decode
}  // namespace Processing
