#include "StateMachine.h"
#include "LINE_Follow.h"
#include "EVAC.h"

#include "../../config.h"

#include <Arduino.h>

namespace StateMachine {

// DEBUG: boot straight into EVAC. Set back to 0 for normal runs.
#define DEBUG_FORCE_START_EVAC 0

namespace {
    RobotState _current = LINE_FOLLOW;
    RobotState _pending = LINE_FOLLOW;
    bool       _hasPending = false;
    bool       _justEntered = true;
}

void init() {
#if DEBUG_FORCE_START_EVAC
    _current = EVAC;
#else
    _current     = LINE_FOLLOW;
#endif
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
        case EVAC:          EVAC::onEnter();          break;
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
        case EVAC:          EVAC::update();          break;
    }
}

}  // namespace StateMachine
