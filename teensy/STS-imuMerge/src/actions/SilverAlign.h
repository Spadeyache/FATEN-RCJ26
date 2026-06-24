#pragma once

// =============================================================================
//  Actions::SilverAlign — rotate the robot perpendicular to the evac silver tape.
//
//  align(): blocking. Puts the XIAO into SILVER_ALIGN mode, P-spins in place to
//  drive the reported tape tilt to ~0 (tape horizontal = robot perpendicular),
//  holds until aligned for a few frames or a timeout, then stops the motors and
//  restores XIAO line-follow mode. Returns true if it converged, false on
//  timeout / tape-not-found.
//
//  Self-contained — call it from an entry sequence; it touches no other state.
// =============================================================================

namespace Actions {
namespace SilverAlign {

bool align();

}  // namespace SilverAlign
}  // namespace Actions
