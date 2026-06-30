#pragma once

// =============================================================================
//  Actions::SilverAlign — wait for evac silver tape.
//
//  align(): blocking. Puts the XIAO into EVAC_COLOR_MASK mode, waits until the
//  simplified mode reports silver for a few frames or times out, then restores
//  XIAO line-follow mode. No angle is used.
//
//  Self-contained — call it from an entry sequence; it touches no other state.
// =============================================================================

namespace Actions {
namespace SilverAlign {

bool align();

}  // namespace SilverAlign
}  // namespace Actions
