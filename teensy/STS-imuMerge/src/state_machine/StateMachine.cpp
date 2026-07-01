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

// DEBUG: boot straight into EVAC_EXIT to bench-test the evac color-mask
// black/silver -> beep path without driving the full course. Pair with the
// XIAO's DEBUG_FORCE_EVAC_COLOR_MASK_MODE. Set back to 0 for normal runs.
#define DEBUG_FORCE_START_EVAC_EXIT 0

namespace {
    constexpr unsigned long RED_STALL_MS = 6150;
    constexpr unsigned long RED_SUPPRESS_MS = 15000;
    constexpr unsigned long RED_CMD_SAMPLE_MS = 20;
    constexpr unsigned long RED_CLEAR_ARM_MS = 150;
    constexpr uint8_t RED_CLEAR_FRAMES = 5;

    RobotState _current = LINE_FOLLOW;
    RobotState _pending = LINE_FOLLOW;
    bool       _hasPending = false;
    bool       _justEntered = true;
    unsigned long _redStallStart = 0;
    unsigned long _redLastCmdSample = 0;
    unsigned long _redSuppressUntil = 0;
    uint8_t _redClearFrames = 0;
}

void init() {
#if DEBUG_FORCE_START_EVAC_EXIT
    _current = EVAC_EXIT;
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

bool redSuppressed() {
    return _redSuppressUntil != 0 && (long)(millis() - _redSuppressUntil) < 0;
}

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
            _redStallStart = millis();
            _redLastCmdSample = _redStallStart;
            _redClearFrames = 0;
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
            // Fixed red pause, then ignore red long enough to drive clear.
            Actions::Drive::stop();
            if (millis() - _redStallStart >= RED_CLEAR_ARM_MS &&
                millis() - _redLastCmdSample >= RED_CMD_SAMPLE_MS) {
                _redLastCmdSample = millis();
                if (Processing::XiaoDecode::command() == FEAT_RED) {
                    _redClearFrames = 0;
                } else if (_redClearFrames <= RED_CLEAR_FRAMES) {
                    _redClearFrames++;
                }
            }
            if (_redClearFrames > RED_CLEAR_FRAMES) {
                Processing::XiaoDecode::clearFilter();
#if PRINT_STATE
                Serial.println("Red cleared early -> LINE_FOLLOW");
#endif
                transitionTo(LINE_FOLLOW);
                break;
            }
            if (millis() - _redStallStart >= RED_STALL_MS) {
                Processing::XiaoDecode::clearFilter();
                _redSuppressUntil = millis() + RED_SUPPRESS_MS;
#if PRINT_STATE
                Serial.println("Red pause done -> LINE_FOLLOW");
#endif
                transitionTo(LINE_FOLLOW);
            }
            break;
    }
}

}  // namespace StateMachine
