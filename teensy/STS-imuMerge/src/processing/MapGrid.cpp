#include "MapGrid.h"
#include <math.h>

void mapInit(EvacMap& m) {
    for (uint16_t i = 0; i < MAP_N; ++i) {
        m.semantic[i] = CS_UNKNOWN;
        m.logodds [i] = 0;
    }
}

void mapClearLogodds(EvacMap& m) {
    for (uint16_t i = 0; i < MAP_N; ++i) m.logodds[i] = 0;
}

// Higher rank wins. Equal-rank incoming overwrites (re-detections refresh).
static inline uint8_t semanticRank(CellSemantic s) {
    switch (s) {
        case CS_VICTIM_ALIVE: return 7;
        case CS_VICTIM_DEAD:  return 6;
        case CS_EVAC_RED:     return 5;
        case CS_EVAC_GRN:     return 5;
        case CS_OBSTACLE:     return 4;
        case CS_WALL:         return 3;
        case CS_FREE:         return 2;
        case CS_UNKNOWN:      return 1;
        default:              return 0;
    }
}

bool semanticCanReplace(CellSemantic current, CellSemantic incoming) {
    return semanticRank(incoming) >= semanticRank(current);
}

static inline void cellLogoddsAdd(EvacMap& m, uint8_t cx, uint8_t cy, int8_t delta) {
    const uint16_t i = idx(cx, cy);
    int16_t v = (int16_t)m.logodds[i] + (int16_t)delta;
    if (v > LO_CLAMP)  v = LO_CLAMP;
    if (v < -LO_CLAMP) v = -LO_CLAMP;
    m.logodds[i] = (int8_t)v;

    // Promote semantic when belief crosses decision threshold.
    const CellSemantic cur = (CellSemantic)m.semantic[i];
    if (v > LO_DECISIVE) {
        if (semanticCanReplace(cur, CS_WALL)) m.semantic[i] = CS_WALL;
    } else if (v < -LO_DECISIVE) {
        if (semanticCanReplace(cur, CS_FREE)) m.semantic[i] = CS_FREE;
    }
}

// Bresenham-style DDA from (cx0, cy0) to (cx1, cy1).
template <typename Fn>
static void rayWalk(int16_t cx0, int16_t cy0, int16_t cx1, int16_t cy1, Fn visit) {
    int16_t dx =  abs(cx1 - cx0);
    int16_t dy = -abs(cy1 - cy0);
    int16_t sx = (cx0 < cx1) ? 1 : -1;
    int16_t sy = (cy0 < cy1) ? 1 : -1;
    int16_t err = dx + dy;

    int16_t x = cx0, y = cy0;
    while (true) {
        const bool endpoint = (x == cx1 && y == cy1);
        visit(x, y, endpoint);
        if (endpoint) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

void mapIntegrateRay(EvacMap& m,
                     float rx_mm, float ry_mm,
                     float bearing_rad,
                     float range_mm,
                     bool  hit_endpoint)
{
    if (range_mm <= 0.0f) return;

    const float ex = rx_mm + range_mm * cosf(bearing_rad);
    const float ey = ry_mm + range_mm * sinf(bearing_rad);

    const int16_t cx0 = worldToCellX(rx_mm);
    const int16_t cy0 = worldToCellY(ry_mm);
    const int16_t cx1 = worldToCellX(ex);
    const int16_t cy1 = worldToCellY(ey);

    if (!inBounds(cx0, cy0)) return;

    rayWalk(cx0, cy0, cx1, cy1, [&](int16_t x, int16_t y, bool endpoint) {
        if (!inBounds(x, y)) return;
        if (endpoint) {
            cellLogoddsAdd(m, (uint8_t)x, (uint8_t)y, hit_endpoint ? LO_HIT : LO_MISS);
        } else {
            if (x == cx0 && y == cy0) return;   // skip robot's own cell
            cellLogoddsAdd(m, (uint8_t)x, (uint8_t)y, LO_MISS);
        }
    });
}

static char glyph(CellSemantic s) {
    switch (s) {
        case CS_FREE:         return ' ';
        case CS_WALL:         return '#';
        case CS_OBSTACLE:     return 'O';
        case CS_EVAC_RED:     return 'R';
        case CS_EVAC_GRN:     return 'G';
        case CS_VICTIM_ALIVE: return 'A';
        case CS_VICTIM_DEAD:  return 'D';
        case CS_UNKNOWN:
        default:              return '.';
    }
}

void mapDumpASCII(const EvacMap& m, float robot_x_mm, float robot_y_mm) {
    const int16_t rcx = worldToCellX(robot_x_mm);
    const int16_t rcy = worldToCellY(robot_y_mm);

    Serial.println(F("---- map: top=+y(left), right=+x(forward), * = robot ----"));
    for (int8_t cy = MAP_DIM - 1; cy >= 0; --cy) {
        for (uint8_t cx = 0; cx < MAP_DIM; ++cx) {
            char ch;
            if (cx == (uint8_t)rcx && cy == (int8_t)rcy) ch = '*';
            else ch = glyph((CellSemantic)m.semantic[idx(cx, (uint8_t)cy)]);
            Serial.write(ch);
        }
        Serial.println();
    }
    Serial.println(F("-------------------------------------------"));
}
