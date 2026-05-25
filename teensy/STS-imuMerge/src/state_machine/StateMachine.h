#pragma once

// =============================================================================
//  StateMachine — top-level robot mode dispatcher.
// =============================================================================

namespace StateMachine {

enum RobotState {
    LINE_FOLLOW,
    LINE_OBSTACLE,
    LINE_GAP,
    STALLED_RED,         // handled inline in StateMachine.cpp (no file)
    EVAC_ENTRY,
    EVAC_SEARCH,
    EVAC_DEPLOY,
    EVAC_EXIT,
};

void       init();
void       tick();                 // dispatches to active state's update()
void       transitionTo(RobotState next);
RobotState current();

}  // namespace StateMachine
