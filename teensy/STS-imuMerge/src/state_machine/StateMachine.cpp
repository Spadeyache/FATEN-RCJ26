#include "StateMachine.h"
#include "LINE_Follow.h"
#include "LINE_Obstacle.h"
#include "LINE_Gap.h"
#include "EVAC_Entry.h"
#include "EVAC_SearchDeploy.h"
#include "EVAC_Exit.h"

#include "../../config.h"
#include "../actions/Drive.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>

namespace StateMachine {

namespace {
    RobotState _current = LINE_FOLLOW;
    RobotState _pending = LINE_FOLLOW;
    bool       _hasPending = false;
    bool       _justEntered = true;
}

void init() {
    _current     = LINE_FOLLOW;
    _justEntered = true;
}

void transitionTo(RobotState next) {
    _pending    = next;
    _hasPending = true;
}

RobotState current() { return _current; }

static void runOnEnter(RobotState s) {
    switch (s) {
        case LINE_FOLLOW:   LINE_Follow::onEnter();   break;
        case LINE_OBSTACLE: LINE_Obstacle::onEnter(); break;
        case LINE_GAP:      LINE_Gap::onEnter();      break;
        case EVAC_ENTRY:        EVAC_Entry::onEnter();        break;
        case EVAC_SEARCH_DEPLOY: EVAC_SearchDeploy::onEnter(); break;
        case EVAC_EXIT:         EVAC_Exit::onEnter();         break;
        case STALLED_RED:
#if PRINT_STATE
            Serial.println("State: STALLED_RED");
#endif
            Actions::Drive::stop();
            break;
    }
}

void tick() {
    if (_justEntered || _hasPending) {
        if (_hasPending) { _current = _pending; _hasPending = false; }
        runOnEnter(_current);
        _justEntered = false;
    }

    switch (_current) {
        case LINE_FOLLOW:   LINE_Follow::update();   break;
        case LINE_OBSTACLE: LINE_Obstacle::update(); break;
        case LINE_GAP:      LINE_Gap::update();      break;
        case EVAC_ENTRY:        EVAC_Entry::update();        break;
        case EVAC_SEARCH_DEPLOY: EVAC_SearchDeploy::update(); break;
        case EVAC_EXIT:         EVAC_Exit::update();         break;

        case STALLED_RED:
            // Idle until XIAO clears the red signal.
            Actions::Drive::stop();
            if (Processing::XiaoDecode::command() != FEAT_RED) {
                Processing::XiaoDecode::clearFilter();
#if PRINT_STATE
                Serial.println("Red cleared â†’ LINE_FOLLOW");
#endif
                transitionTo(LINE_FOLLOW);
            }
            break;
    }
}

}  // namespace StateMachine
