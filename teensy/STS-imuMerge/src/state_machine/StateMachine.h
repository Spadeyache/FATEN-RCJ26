#pragma once

// =============================================================================
//  StateMachine — top-level robot mode dispatcher.
// =============================================================================

namespace StateMachine {

enum RobotState {
    LINE_FOLLOW,
    EVAC,
};

void       init();
void       tick();                 // dispatches to active state's update()
void       transitionTo(RobotState next);
RobotState current();

}  // namespace StateMachine
