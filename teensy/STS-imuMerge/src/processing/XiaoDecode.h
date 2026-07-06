#pragma once

// =============================================================================
//  Processing::XiaoDecode - interprets the XIAO register cache.
//
//  Reads raw registers via Sensors::XIAO_link::get(reg), feeds the feature
//  byte through CommandFilter at 50 Hz, and exposes typed getters.
// =============================================================================

#include <stdint.h>
#include "../../config.h"
#include "CommandFilter.h"

namespace Processing {
namespace XiaoDecode {

void tick(bool instantRun = false);  // re-runs filter every 20 ms (or instantly)

uint8_t command();        // confirmed FEAT_* event (FEAT_NONE if none)
float   lineError();      // 0..254
float   gapAngle();       // 0..254, 127 = straight
bool    commitFlag();     // XIAO green-turn commit in progress
bool    tightSlowFlag();  // XIAO tight-turn target is low in frame
bool    silverSeen();
bool    evacBlackSeen();
bool    gapFrontFlag();

void    setMode(XiaoMode m);
void    clearFilter();
void    setCommand(uint8_t c);

const CommandFilter& filter();

}  // namespace XiaoDecode
}  // namespace Processing
