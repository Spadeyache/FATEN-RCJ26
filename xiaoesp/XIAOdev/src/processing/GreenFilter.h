#ifndef GREENFILTER_H
#define GREENFILTER_H

// =============================================================================
//  GreenFilter — rolling-window vote confirmation of green markers, migrated
//  from the Teensy CommandFilter green logic onto the XIAO.
//
//  Feed one raw per-frame observation; get back a confirmed code once enough
//  votes accumulate in the window:
//      raw / return code:  0 = none, 1 = U-turn (both), 2 = left, 3 = right
// =============================================================================

#include <stdint.h>

void    gf_reset();                  // clear the vote window (e.g. after acting)
uint8_t gf_update(uint8_t raw);      // push one observation, return confirmed code

#endif // GREENFILTER_H

