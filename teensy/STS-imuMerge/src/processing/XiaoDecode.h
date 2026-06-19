#pragma once

// =============================================================================
//  Processing::XiaoDecode â€” interprets the XIAO register cache.
//
//  Reads raw registers via Sensors::XIAO_link::get(reg), feeds the feature
//  byte through CommandFilter at 50 Hz, and exposes typed getters:
//      command()    â€” confirmed XIAO_FEAT_* code (see CommandFilter)
//      lineError()  â€” line COM 0..254 (127 = centre)
//      gapAngle()   â€” gap angle 0..254 (127 = 0Â°), mode 3 only
//
//  Also owns the active XIAO mode (XIAO_MODE_*) so callers can swap modes
//  via setMode() without reaching into the link layer.
// =============================================================================

#include <stdint.h>
#include "../../config.h"
#include "CommandFilter.h"

namespace Processing {
namespace XiaoDecode {

void tick(bool instantRun = false);  // re-runs filter every 20 ms (or instantly)

uint8_t command();        // confirmed FEAT_* event (FEAT_NONE if none)
float   lineError();      // 0..254
float   gapAngle();       // 0..254
bool    commitFlag();     // XIAO green-turn commit in progress → freeze transitions

void    setMode(XiaoMode m);
void    clearFilter();    // forget votes after a mode change / state transition
void    setCommand(uint8_t c);   // override (used to consume a command without re-firing)

const CommandFilter& filter();    // for debug votes printing

}  // namespace XiaoDecode
}  // namespace Processing
