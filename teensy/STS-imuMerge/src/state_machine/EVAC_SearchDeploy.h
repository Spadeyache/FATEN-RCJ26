#pragma once

// =============================================================================
//  EVAC_SearchDeploy — collect SILVER (live) victims only, on the single
//  global 1:30 evac clock (EVAC_Entry::GLOBAL_TIMEOUT_MS, from zone entry).
//
//  Loop (blocking, until the clock runs out):
//    - if 2 silvers held -> deploy at the GREEN corner -> keep collecting
//    - else spin/forward-search for a silver -> run at it -> grab via
//      VictimManager (self-confirms; a failed grab counts nothing)
//  Timer is only checked BETWEEN whole actions, never mid-grab/mid-deploy.
//  On timeout while still holding a silver: one final green deploy (clock
//  ignored), then -> EVAC_EXIT. Dead/red code remains but is unreachable.
// =============================================================================

namespace EVAC_SearchDeploy {

void onEnter();
void update();

}  // namespace EVAC_SearchDeploy
