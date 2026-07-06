#pragma once
// =============================================================================
//  ColorAssign — maps collected ball colours to deploy numbers 1..4.
//
//  Runs on the FATEN XIAO. Inputs are the colours each robot grabbed in EVAC
//  (faten's two + kavosh's one/two, as colordet class ids). Output is a deploy
//  number 1..4 per ball = the GREEN_RIGHT_TURN_COUNT at which that ball is
//  dropped.
//
//  The field is one of four layouts. Each layout fixes which colour sits at
//  each of the four deploy corners (columns 1..4):
//
//      col:     1        2       3            4
//    SANG:   Yellow   Black   Green        Blue/Orange
//    RANG:   Red      Black   Green        Blue/Orange
//    SEGE:   Yellow   Silver  Blue/Orange  Silver
//    GARA:   Blue/Orange Black Red         Black
//
//  We don't know the layout up front — we infer the most probable one from the
//  colours actually collected, then read off each ball's column. Blue and
//  Orange are one interchangeable class. Duplicate-colour layouts (SEGE has two
//  Silver, GARA has two Black) let two same-colour balls take the two columns,
//  so assignment is done as a distinct-slot match, not a per-colour lookup.
//
//  Header-only and free of Arduino deps (uint8_t only) so it can be unit
//  tested on a host. See ColorAssign_test.cpp.
// =============================================================================

#include <stdint.h>

namespace ColorAssign {

// colordet.kmodel class ids (datasets/colordet classes.txt), mirrored from
// teensy config.h so this module stands alone.
enum : uint8_t {
    CD_BLACK  = 0,
    CD_BLUE   = 1,
    CD_GREEN  = 2,
    CD_ORANGE = 3,
    CD_RED    = 4,
    CD_SILVER = 5,
    CD_YELLOW = 6,
    CD_NONE   = 255,   // empty gripper — ignored
};

enum Layout : uint8_t { SANG = 0, RANG = 1, SEGE = 2, GARA = 3, LAYOUT_COUNT = 4 };

// Internal colour classes (Blue and Orange collapse to BO).
enum : uint8_t { CL_YELLOW = 0, CL_BLACK, CL_GREEN, CL_BO, CL_RED, CL_SILVER, CL_COUNT };

struct Result {
    uint8_t number[4];   // 1..4 per input ball, 0 = empty input or unassignable
    uint8_t layout;      // chosen Layout
    float   confidence;  // posterior of the chosen layout, 0..1
    uint8_t ballCount;   // how many non-empty inputs were seen
};

namespace detail {

// Column colour classes per layout, columns 1..4 in index 0..3.
inline const uint8_t (&layoutCols())[LAYOUT_COUNT][4] {
    static const uint8_t cols[LAYOUT_COUNT][4] = {
        /* SANG */ { CL_YELLOW, CL_BLACK,  CL_GREEN, CL_BO     },
        /* RANG */ { CL_RED,    CL_BLACK,  CL_GREEN, CL_BO     },
        /* SEGE */ { CL_YELLOW, CL_SILVER, CL_BO,    CL_SILVER },
        /* GARA */ { CL_BO,     CL_BLACK,  CL_RED,   CL_BLACK  },
    };
    return cols;
}

inline uint8_t toClass(uint8_t cdId) {
    switch (cdId) {
        case CD_YELLOW: return CL_YELLOW;
        case CD_BLACK:  return CL_BLACK;
        case CD_GREEN:  return CL_GREEN;
        case CD_BLUE:
        case CD_ORANGE: return CL_BO;
        case CD_RED:    return CL_RED;
        case CD_SILVER: return CL_SILVER;
        default:        return 0xFF;   // unknown / none
    }
}

inline uint32_t nCr(uint8_t n, uint8_t r) {
    if (r > n) return 0;
    if (r == 0 || r == n) return 1;
    uint32_t num = 1, den = 1;
    for (uint8_t i = 0; i < r; ++i) { num *= (n - i); den *= (i + 1); }
    return num / den;
}

}  // namespace detail

// colors[4]: up to four balls as colordet ids, CD_NONE for an empty gripper.
// A typical caller order is { fatenLeft, fatenRight, kavoshLeft, kavoshRight }.
// Result.number[i] aligns with colors[i].
inline Result compute(const uint8_t colors[4]) {
    using namespace detail;
    Result r{};
    for (int i = 0; i < 4; ++i) r.number[i] = 0;

    // 1) collapse inputs to colour classes + observed per-class counts
    uint8_t cls[4];
    uint8_t obs[CL_COUNT] = {0};
    for (int i = 0; i < 4; ++i) {
        cls[i] = toClass(colors[i]);
        if (cls[i] < CL_COUNT) { obs[cls[i]]++; r.ballCount++; }
    }
    if (r.ballCount == 0) { r.layout = SANG; r.confidence = 0.0f; return r; }

    // 2) per-class slot counts for each layout
    uint8_t slot[LAYOUT_COUNT][CL_COUNT] = {{0}};
    const auto& cols = layoutCols();
    for (int L = 0; L < LAYOUT_COUNT; ++L)
        for (int c = 0; c < 4; ++c) slot[L][cols[L][c]]++;

    // 3) likelihood weight per layout = product of C(slots, observed)
    uint32_t weight[LAYOUT_COUNT];
    uint32_t total = 0;
    for (int L = 0; L < LAYOUT_COUNT; ++L) {
        uint32_t w = 1;
        for (int c = 0; c < CL_COUNT; ++c) w *= nCr(slot[L][c], obs[c]);
        weight[L] = w;             // 0 => layout cannot hold these colours
        total += w;
    }

    // 4) pick the most probable feasible layout (tie -> lowest enum)
    int best = -1;
    for (int L = 0; L < LAYOUT_COUNT; ++L)
        if (weight[L] > 0 && (best < 0 || weight[L] > weight[best])) best = L;

    if (best < 0) {
        // No layout fits every colour (e.g. two Blue/Orange balls). Degrade:
        // choose the layout matching the most balls, assign what we can.
        int bestMatch = -1, bestL = SANG;
        for (int L = 0; L < LAYOUT_COUNT; ++L) {
            int m = 0;
            for (int c = 0; c < CL_COUNT; ++c)
                m += (obs[c] < slot[L][c] ? obs[c] : slot[L][c]);
            if (m > bestMatch) { bestMatch = m; bestL = L; }
        }
        best = bestL;
        r.confidence = 0.0f;       // flag: uncertain, partial assignment
    } else {
        r.confidence = (float)weight[best] / (float)total;
    }
    r.layout = (uint8_t)best;

    // 5) distinct-slot assignment in the chosen layout
    bool used[4] = {false, false, false, false};
    for (int i = 0; i < 4; ++i) {
        if (cls[i] >= CL_COUNT) continue;         // empty gripper
        for (int c = 0; c < 4; ++c) {
            if (!used[c] && cols[best][c] == cls[i]) {
                used[c] = true;
                r.number[i] = (uint8_t)(c + 1);   // columns are 1-based
                break;
            }
        }
    }
    return r;
}

}  // namespace ColorAssign
