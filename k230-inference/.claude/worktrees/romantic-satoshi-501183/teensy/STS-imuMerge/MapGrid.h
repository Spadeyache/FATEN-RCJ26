#ifndef MapGrid_h
#define MapGrid_h

// =============================================================================
//  MapGrid — 40x40 occupancy + semantic grid for the evacuation zone.
//
//  Two parallel arrays of MAP_DIM*MAP_DIM bytes each:
//    semantic[]  — CellSemantic enum (UNKNOWN, FREE, WALL, OBSTACLE, EVAC_*, VICTIM_*)
//    logodds[]   — int8 occupancy belief, clamped to [-LO_CLAMP, +LO_CLAMP].
//                  Negative = free, positive = occupied.
//
//  Coordinate convention (robot-frame):
//    World origin (0, 0) mm  ↔  cell (MAP_ORIGIN_CX, MAP_ORIGIN_CY) = (0, 20).
//    +x_world = forward into zone from entrance      → cell column index +
//    +y_world = robot's left at entry                → cell row index +
//    Cell size: MAP_CELL_MM (30 mm).
//    Forward span: 0..40 cells = 0..1200 mm.
//    Lateral span: -20..+20 cells = -600..+570 mm.
// =============================================================================

#include <Arduino.h>
#include <math.h>
#include "config.h"

constexpr uint8_t  MAP_ORIGIN_CX = 0;
constexpr uint8_t  MAP_ORIGIN_CY = MAP_DIM / 2;
constexpr uint16_t MAP_N         = (uint16_t)MAP_DIM * MAP_DIM;

enum CellSemantic : uint8_t {
    CS_UNKNOWN       = 0,
    CS_FREE          = 1,
    CS_WALL          = 2,
    CS_OBSTACLE      = 3,
    CS_EVAC_RED      = 4,
    CS_EVAC_GRN      = 5,
    CS_VICTIM_ALIVE  = 6,
    CS_VICTIM_DEAD   = 7,
};

struct EvacMap {
    uint8_t semantic[MAP_N];
    int8_t  logodds [MAP_N];
};

// --- Lifecycle ---
void mapInit(EvacMap& m);
void mapClearLogodds(EvacMap& m);   // keep semantics, zero log-odds (post-EEPROM-load)

// --- Coordinate conversion ---
inline int16_t worldToCellX(float x_mm) {
    return (int16_t)MAP_ORIGIN_CX + (int16_t)floorf(x_mm / (float)MAP_CELL_MM);
}
inline int16_t worldToCellY(float y_mm) {
    return (int16_t)MAP_ORIGIN_CY + (int16_t)floorf(y_mm / (float)MAP_CELL_MM);
}
inline bool inBounds(int16_t cx, int16_t cy) {
    return cx >= 0 && cx < MAP_DIM && cy >= 0 && cy < MAP_DIM;
}
inline uint16_t idx(uint8_t cx, uint8_t cy) {
    return (uint16_t)cy * MAP_DIM + (uint16_t)cx;
}

// --- Semantic precedence (returns true if `incoming` is allowed to overwrite `current`) ---
bool semanticCanReplace(CellSemantic current, CellSemantic incoming);

// --- ToF ray integration ---
//  Cast a ray from world (rx, ry) for distance r along bearing in robot-frame.
//  Each traversed cell gets LO_MISS; endpoint cell gets LO_HIT (only if hit_endpoint).
//  Updates semantic when |logodds| crosses LO_DECISIVE.
void mapIntegrateRay(EvacMap& m,
                     float rx_mm, float ry_mm,
                     float bearing_rad,
                     float range_mm,
                     bool  hit_endpoint);

// --- ASCII dump over Serial ---
//  '.' unknown, ' ' free, '#' wall, 'O' obstacle, 'R' evac-red, 'G' evac-green,
//  'A' alive victim, 'D' dead victim, '*' robot position.
void mapDumpASCII(const EvacMap& m, float robot_x_mm, float robot_y_mm);

#endif
