#pragma once

// =============================================================================
//  Processing::CommandFilter — moving-average (ring-buffer) vote on the XIAO
//  FEATURE byte. An event is confirmed once it reaches its FILTER_THRESHOLD_*
//  vote count within the last FILTER_QUEUE_SIZE frames. Events:
//      FEAT_RED / FEAT_SILVER / FEAT_UTURN / FEAT_LINE_LOST  (see config.h)
//
//  Vote totals are exposed as members for debug logging.
// =============================================================================

#include <stdint.h>
#include "../../config.h"

class CommandFilter {
public:
    uint8_t votesUturn    = 0;
    uint8_t votesRed      = 0;
    uint8_t votesSilver   = 0;
    uint8_t votesLineLost = 0;
    uint8_t votesSearchLineBlack = 0;  // SEARCH_LINE-mode black return line — used by LINE_Obstacle

    CommandFilter() { clear(); }

    void    clear();
    uint8_t update(uint8_t rawCmd);   // returns the confirmed FEAT_* code (FEAT_NONE if none)

private:
    uint8_t _queue[FILTER_QUEUE_SIZE];
    uint8_t _head = 0;
};
