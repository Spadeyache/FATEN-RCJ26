#pragma once

// =============================================================================
//  MapGrid — 40x40 occupancy + semantic grid for the evacuation zone.
//
//  Two parallel arrays of MAP_DIM*MAP_DIM bytes each:
//    semantic[]  — CellSemantic enum
//    logodds[]   — int8 occupancy belief, clamped to [-LO_CLAMP, +LO_CLAMP].
//                  Negative = free, positive = occupied.
//
//  World origin (0, 0) ↔ cell (MAP_ORIGIN_CX, MAP_ORIGIN_CY) = (0, 20).
//  +x_world = forward into zone (column index +).
//  +y_world = robot's left at entry (row index +).
//  Cell size: MAP_CELL_MM (30 mm).
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

void mapInit(EvacMap& m);
void mapClearLogodds(EvacMap& m);

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

bool semanticCanReplace(CellSemantic current, CellSemantic incoming);

void mapIntegrateRay(EvacMap& m,
                     float rx_mm, float ry_mm,
                     float bearing_rad,
                     float range_mm,
                     bool  hit_endpoint);

void mapDumpASCII(const EvacMap& m, float robot_x_mm, float robot_y_mm);
