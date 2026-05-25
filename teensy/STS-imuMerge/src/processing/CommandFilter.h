#pragma once

// =============================================================================
//  Processing::CommandFilter — majority-vote ring buffer for XIAO command byte.
//
//  A command is confirmed once it reaches FILTER_THRESHOLD_<kind> votes within
//  the last FILTER_QUEUE_SIZE frames. Multi-cause logic (U-turn fires when
//  BOTH left- and right-green are glimpsed in the same window) is embedded in
//  update() — see comments inside.
//
//  Vote totals are exposed as members for debug logging.
// =============================================================================

#include <stdint.h>
#include "config.h"

class CommandFilter {
public:
    uint8_t votesUturn  = 0;
    uint8_t votesLeft   = 0;
    uint8_t votesRight  = 0;
    uint8_t votesRed    = 0;
    uint8_t votesSilver = 0;
    uint8_t votesBlack  = 0;
    uint8_t votesNoLine = 0;

    CommandFilter() { clear(); }

    void    clear();
    uint8_t update(uint8_t rawCmd);   // returns the currently confirmed command

private:
    uint8_t _queue[FILTER_QUEUE_SIZE];
    uint8_t _head = 0;
};
