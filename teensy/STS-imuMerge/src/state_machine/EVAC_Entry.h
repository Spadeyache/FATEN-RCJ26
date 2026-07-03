#pragma once

namespace EVAC_Entry {

// Hard ceiling on the whole evac phase (entry-hunt + search/deploy),
// measured from onEnter() (evac zone entry). EVAC_SearchDeploy folds this
// into its own search timeout; EVAC_Entry's own hunt loop checks it directly
// and skips straight to EVAC_EXIT if the clock runs out before a victim is
// ever found.
constexpr unsigned long GLOBAL_TIMEOUT_MS = 90000UL;  //90000 1:30 - the ONE evac clock (entry-hunt + search + deploy)

void onEnter();
void update();
unsigned long startMs();  // millis() timestamp of onEnter() (evac zone entry)

}  // namespace EVAC_Entry
