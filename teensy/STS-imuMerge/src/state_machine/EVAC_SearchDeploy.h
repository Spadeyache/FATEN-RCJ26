#pragma once

// =============================================================================
//  EVAC_SearchDeploy — collect victims and deploy them, on a 2-minute budget.
//
//  Loop (blocking, for EVAC_SEARCH_TIMEOUT_MS):
//    - if 3 balls held -> deploy -> clear -> keep collecting
//    - else spin to find a ball -> run at it -> grab via VictimManager
//      (grab self-confirms; a failed grab counts nothing and we keep going)
//  When the timer expires: do a final deploy if holding any, then -> EVAC_EXIT.
// =============================================================================

namespace EVAC_SearchDeploy {

void onEnter();
void update();

}  // namespace EVAC_SearchDeploy
