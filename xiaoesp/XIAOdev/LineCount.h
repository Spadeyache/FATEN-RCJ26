#ifndef LINECOUNT_H
#define LINECOUNT_H

// =============================================================================
//  LineCount — detect how many lines cross the analysis-ROI border, and
//  classify them into 1 "in" (the line the robot is on) + N "out" (branches).
//
//  Layer 1: lc_detectCrossings()  — walk the ROI rectangle border as a single
//           1-D loop, run-count contiguous black spans → list of Crossings.
//  Layer 2: lc_updateIn()         — track the in-point frame-to-frame by
//           nearest-with-gating; base case = lowest-pixel (nearest robot).
//
//  Standalone primitives: they do NOT touch the FEATURE/COM registers or motion.
// =============================================================================

#include <Arduino.h>
#include "esp_camera.h"

#define LC_MAX_CROSSINGS 16

// Edge ids around the loop.
enum LcEdge : uint8_t { LC_EDGE_BOTTOM = 0, LC_EDGE_RIGHT = 1, LC_EDGE_TOP = 2, LC_EDGE_LEFT = 3 };

struct Crossing {
    float   pos;     // location along the perimeter loop, in sample units [0, perimeter)
    uint8_t edge;    // LcEdge at the run midpoint
    uint8_t pixelX;  // image column at the run midpoint
    uint8_t pixelY;  // image row at the run midpoint (largest pixelY = nearest the robot)
    uint8_t width;   // run length in perimeter samples
};

struct LineCounts {
    Crossing crossings[LC_MAX_CROSSINGS];
    uint8_t  count;       // number of crossings found
    float    perimeter;   // total loop length (sample count) — for circular distance
};

// Result of classifying crossings into in + outs.
struct LineClass {
    int8_t  inIndex;   // index into LineCounts.crossings chosen as IN, or -1 if none/unseen
    uint8_t outCount;  // number of crossings that are NOT the in
    bool    inHeld;    // true when IN could not be matched this frame (previous pos held)
    float   inPos;     // perimeter pos of the IN (valid when inIndex >= 0)
};

// ── Layer 1 ──────────────────────────────────────────────────────────────────
// Detect every line crossing the ROI border. Fills `out`.
void lc_detectCrossings(camera_fb_t* fb, LineCounts& out);

// ── Layer 2 helpers ──────────────────────────────────────────────────────────
// Circular distance between two perimeter positions.
float lc_loopDist(float a, float b, float perimeter);

// Reusable nearest-with-gating tracker (used for the in-point AND a committed
// goal exit). Returns the index of the crossing nearest `prevPos`, or -1 if the
// nearest is farther than LC_MATCH_GATE (i.e. that line was not seen this frame)
// or there are no crossings.
int lc_trackPoint(float prevPos, const LineCounts& lc);

// Base case: index of the crossing with the lowest pixel (largest pixelY,
// nearest the robot/bottom), or -1 if there are none.
int lc_lowestPixel(const LineCounts& lc);

// Stateful per-frame classification of in + outs. Tracks the in-point across
// frames; re-seeds from the base case on the first frame or after the in has
// been unseen for LC_LOST_FRAMES frames. Holds the previous in-pos on a
// transient miss (gate reject) instead of jumping to a branch.
LineClass lc_updateIn(const LineCounts& lc);

// Forget the tracked in-point (e.g. on a mode/state change) so the next frame
// re-seeds from the base case.
void lc_resetTracking();

// ── Line-follow error ────────────────────────────────────────────────────────
// Focused-out crossing: the out (any crossing that is not `inIndex`) closest to
// the view centre, tie-broken toward the higher one (smaller pixelY). -1 if none.
int  lc_focusedOut(const LineCounts& lc, int inIndex);

// Line error (0..254, 127 = centred) from pixel-space lookahead/position error.
// The focused out-point is the main target; the in-point is a small stabilizer.
// Returns false when in/out are not both available (caller should hold last).
bool lc_slopeError(const LineCounts& lc, int inIndex, int outIndex, float& errOut, float* errPxOut = nullptr);

// ── Debug bridge (for the esp32_camera_viewer overlay) ───────────────────────
// The mode (Core 1) stores the latest result; the stream task (Core 0) formats
// it into a "[LC] ..." text line and emits it under the serial mutex, so the
// viewer can draw the ROI box + in/out crossing points over the camera image.
void lc_storeDebug(const LineCounts& lc, const LineClass& cls, int focusedOut, int errByte, float errPx);

// Build a "[LC] box=.. n=.. in=.. fo=.. xi=.. xo=.. epx=.. err=.. ..." line
// (newline-terminated, ASCII only) into buf. Returns bytes written, or 0.
int  lc_formatDebug(char* buf, int bufLen);

#endif // LINECOUNT_H
