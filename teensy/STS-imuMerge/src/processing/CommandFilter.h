#pragma once

// =============================================================================
//  Processing::CommandFilter — majority-vote ring buffer for the XIAO command
//  byte. The XIAO now sends the RAW per-frame reading (no XIAO-side filtering);
//  all debouncing happens here.
//
//  A command is confirmed once it reaches its FILTER_THRESHOLD_<kind> votes
//  within the last FILTER_QUEUE_SIZE frames. Multi-cause logic (U-turn fires
//  when BOTH left- and right-green are glimpsed in the same window) is embedded
//  in update() — see comments inside.
//
//  Vote totals are exposed as members for debug logging.
// =============================================================================

#include <stdint.h>
#include "../../config.h"

class CommandFilter {
public:
    uint8_t votesLeft   = 0;   // u-turn is derived from votesLeft + votesRight, no separate type
    uint8_t votesRight  = 0;
    uint8_t votesSilver = 0;
    uint8_t votesBlack  = 0;   // LINE-mode saturated black intersection row
    uint8_t votesSearchLineBlack = 0;  // SEARCH_LINE-mode black return line, used by uturn recovery
    uint8_t votesLineLost = 0;

    CommandFilter() { clear(); }

    void    clear();
    uint8_t update(uint8_t rawCmd);   // returns the confirmed FEAT_* code (FEAT_NONE if none)

private:
    uint8_t _queue[FILTER_QUEUE_SIZE];
    uint8_t _head = 0;
};
