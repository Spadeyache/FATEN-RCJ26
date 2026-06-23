#include "XiaoStream.h"
#include "../config/config.h"

namespace {

struct SensorSlot {
    XiaoStreamClass cls;
    XiaoStreamPriority prio;
};

struct StreamEvent {
    const char* action;
    char side;
    uint8_t x;
    uint8_t y;
    char reason[16];
};

constexpr uint8_t EVENT_QUEUE_SIZE = 8;

LineCounts s_dbgLc;
LineClass  s_dbgCls;
int        s_dbgFo = -1;
int        s_dbgErr = LF_ERROR_CENTER;
float      s_dbgErrPx = 0.0f;
bool       s_dbgValid = false;
int        s_dbgSteer = -1;
bool       s_dbgCommitActive = false;
bool       s_dbgCommitLocked = false;
int        s_dbgCommitProgress = 0;
uint8_t    s_dbgGreen = 0;
uint16_t   s_dbgArcBlackCount = 0;
bool       s_dbgArcBlackSaturated = false;

SensorSlot s_sensor[2] = {
    { XS_WHITE, XS_PRIO_NONE },
    { XS_WHITE, XS_PRIO_NONE },
};
bool s_sensorValid = false;
StreamEvent s_events[EVENT_QUEUE_SIZE] = {};
uint8_t s_eventHead = 0;
uint8_t s_eventTail = 0;
uint8_t s_eventCount = 0;

const char* className(XiaoStreamClass c) {
    switch (c) {
        case XS_GREEN:  return "green";
        case XS_RED:    return "red";
        case XS_SILVER: return "silver";
        case XS_BLACK:  return "black";
        default:        return "white";
    }
}

void setSlot(SensorSlot& slot, XiaoStreamClass cls, XiaoStreamPriority prio) {
    if (prio >= slot.prio) {
        slot.cls = cls;
        slot.prio = prio;
        s_sensorValid = true;
    }
}

void pushEvent(const char* action, char side, uint8_t x, uint8_t y, const char* reason) {
    StreamEvent& e = s_events[s_eventHead];
    e.action = action;
    e.side = side;
    e.x = x;
    e.y = y;
    e.reason[0] = '\0';
    if (reason) {
        snprintf(e.reason, sizeof(e.reason), "%s", reason);
    }

    s_eventHead = (uint8_t)((s_eventHead + 1) % EVENT_QUEUE_SIZE);
    if (s_eventCount < EVENT_QUEUE_SIZE) {
        s_eventCount++;
    } else {
        s_eventTail = (uint8_t)((s_eventTail + 1) % EVENT_QUEUE_SIZE);
    }
}

}  // namespace

void xs_beginSensorFrame() {
    s_sensor[XS_LEFT]  = { XS_WHITE, XS_PRIO_NONE };
    s_sensor[XS_RIGHT] = { XS_WHITE, XS_PRIO_NONE };
    s_sensorValid = true;
}

void xs_setSensorBoth(XiaoStreamClass cls, XiaoStreamPriority prio) {
    setSlot(s_sensor[XS_LEFT], cls, prio);
    setSlot(s_sensor[XS_RIGHT], cls, prio);
}

void xs_setSensorSide(XiaoStreamSide side, XiaoStreamClass cls, XiaoStreamPriority prio) {
    if (side > XS_RIGHT) return;
    setSlot(s_sensor[side], cls, prio);
}

void xs_storeLineDebug(const LineCounts& lc, const LineClass& cls, int focusedOut,
                       int errByte, float errPx) {
    s_dbgLc = lc;
    s_dbgCls = cls;
    s_dbgFo = focusedOut;
    s_dbgErr = errByte;
    s_dbgErrPx = errPx;
    s_dbgValid = true;
}

void xs_storeLineSteer(int steerOut, bool commitActive, bool commitLocked,
                       int commitProgress, uint8_t greenCmd,
                       uint16_t arcBlackCount, bool arcBlackSaturated) {
    s_dbgSteer = steerOut;
    s_dbgCommitActive = commitActive;
    s_dbgCommitLocked = commitLocked;
    s_dbgCommitProgress = commitProgress;
    s_dbgGreen = greenCmd;
    s_dbgArcBlackCount = arcBlackCount;
    s_dbgArcBlackSaturated = arcBlackSaturated;
}

void xs_noteCommitStart(bool left) {
    pushEvent("start", left ? 'L' : 'R', 255, 255, nullptr);
}

void xs_noteCommitLock(bool left, uint8_t x, uint8_t y) {
    pushEvent("lock", left ? 'L' : 'R', x, y, nullptr);
}

void xs_noteCommitEnd(const char* reason) {
    pushEvent("end", '-', 255, 255, reason);
}

int xs_formatLineDebug(char* buf, int bufLen) {
    if (!s_dbgValid || bufLen < 48) return 0;

    int o = snprintf(buf, bufLen,
        "[LC] n=%d in=%d fo=%d steer=%d act=%d lock=%d prog=%d oc=%d g=%d ab=%u as=%d err=%d p=",
        s_dbgLc.count, s_dbgCls.inIndex, s_dbgFo, s_dbgSteer,
        s_dbgCommitActive ? 1 : 0, s_dbgCommitLocked ? 1 : 0,
        s_dbgCommitProgress, s_dbgCls.outCount, s_dbgGreen,
        s_dbgArcBlackCount, s_dbgArcBlackSaturated ? 1 : 0, s_dbgErr);

    int nshow = s_dbgLc.count;
    if (nshow > LC_MAX_CROSSINGS) nshow = LC_MAX_CROSSINGS;
    for (int i = 0; i < nshow && o < bufLen - 16; i++) {
        char role = 'o';
        if (i == s_dbgCls.inIndex) role = 'i';
        else if (s_dbgCommitActive && i == s_dbgSteer) role = 'c';
        else if (i == s_dbgFo) role = 'f';

        o += snprintf(buf + o, bufLen - o, "%s%d,%d,%c,%d",
            (i ? " " : ""),
            s_dbgLc.crossings[i].pixelX,
            s_dbgLc.crossings[i].pixelY,
            role,
            s_dbgLc.crossings[i].width);
    }
    if (o < bufLen - 1) {
        buf[o++] = '\n';
        buf[o] = '\0';
    }
    return o;
}

int xs_formatSensorRow(char* buf, int bufLen) {
    if (!s_sensorValid || bufLen < 24) return 0;
    return snprintf(buf, bufLen, "[ROW] l=%s r=%s\n",
        className(s_sensor[XS_LEFT].cls),
        className(s_sensor[XS_RIGHT].cls));
}

int xs_formatEvent(char* buf, int bufLen) {
    if (s_eventCount == 0 || bufLen < 32) return 0;

    StreamEvent e = s_events[s_eventTail];
    s_eventTail = (uint8_t)((s_eventTail + 1) % EVENT_QUEUE_SIZE);
    s_eventCount--;

    if (e.reason[0]) {
        return snprintf(buf, bufLen, "[EVT] kind=commit action=%s side=%c x=%u y=%u reason=%s\n",
                        e.action, e.side, e.x, e.y, e.reason);
    }
    return snprintf(buf, bufLen, "[EVT] kind=commit action=%s side=%c x=%u y=%u\n",
                    e.action, e.side, e.x, e.y);
}

