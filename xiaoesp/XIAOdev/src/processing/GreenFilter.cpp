#include "GreenFilter.h"
#include "../config/config.h"

// Rolling window of raw observations (0 none, 1 uturn, 2 left, 3 right).
static uint8_t s_q[GF_QUEUE_SIZE];
static uint8_t s_head = 0;
static bool    s_init = false;

void gf_reset() {
    for (int i = 0; i < GF_QUEUE_SIZE; i++) s_q[i] = 0;
    s_head = 0;
    s_init = true;
}

uint8_t gf_update(uint8_t raw) {
    if (!s_init) gf_reset();

    s_q[s_head] = raw;
    s_head = (s_head + 1) % GF_QUEUE_SIZE;

    uint8_t vL = 0, vR = 0, vU = 0;
    for (int i = 0; i < GF_QUEUE_SIZE; i++) {
        switch (s_q[i]) {
            case 1: vU++; vL++; vR++; break;   // U-turn counts toward both sides too
            case 2: vL++; break;
            case 3: vR++; break;
            default: break;
        }
    }

    // U-turn wins: sustained both-green, or strong votes on both sides.
    if (vU >= GF_UTURN_VOTES || (vL >= GF_BOTH_VOTES && vR >= GF_BOTH_VOTES)) return 1;
    if (vL >= GF_VOTES) return 2;
    if (vR >= GF_VOTES) return 3;
    return 0;
}

