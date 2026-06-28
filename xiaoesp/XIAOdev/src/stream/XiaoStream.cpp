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
constexpr uint8_t EVAC_MASK_MAX = 96;

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

bool       s_laValid = false;
uint8_t    s_laCount = 0;
bool       s_laTwoDetected = false;
bool       s_laBottomLine = false;
bool       s_laCircleAngle = false;
int        s_laBaseX = -1;
int        s_laBaseY = -1;
uint8_t    s_laBaseWidth = 0;
int        s_laTipX = -1;
int        s_laTipY = -1;
uint8_t    s_laTipWidth = 0;
float      s_laAngleDeg = 0.0f;
uint8_t    s_laEncodedAngle = 127;
uint8_t    s_laAvgY = 0;
uint8_t    s_laFlag = 0;
bool       s_laFineValid = false;
float      s_laFineAngleDeg = 0.0f;
uint8_t    s_laEncodedFineAngle = 127;
int        s_laFineFarX = -1;
int        s_laFineFarY = -1;
uint8_t    s_laFineFarWidth = 0;
int        s_laFineNearX = -1;
int        s_laFineNearY = -1;
uint8_t    s_laFineNearWidth = 0;

bool       s_saValid = false;
bool       s_saSeen = false;
char       s_saCls[8] = "none";
float      s_saAngleDeg = 0.0f;
uint8_t    s_saEncodedAngle = 127;
uint16_t   s_saCount = 0;
uint16_t   s_saFlashCount = 0;
uint16_t   s_saBlackCount = 0;
uint8_t    s_saMinX = 0;
uint8_t    s_saMinY = 0;
uint8_t    s_saMaxX = 0;
uint8_t    s_saMaxY = 0;
uint8_t    s_saMaskX[EVAC_MASK_MAX] = {};
uint8_t    s_saMaskY[EVAC_MASK_MAX] = {};
uint8_t    s_saMaskCount = 0;

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

void xs_storeLineAngleDebug(uint8_t count, bool twoDetected, bool bottomLine,
                            bool circleAngle, int baseX, int baseY,
                            uint8_t baseWidth, int tipX, int tipY,
                            uint8_t tipWidth, float angleDeg,
                            uint8_t encodedAngle, uint8_t avgY,
                            uint8_t flag, bool fineValid,
                            float fineAngleDeg, uint8_t encodedFineAngle,
                            int fineFarX, int fineFarY, uint8_t fineFarWidth,
                            int fineNearX, int fineNearY, uint8_t fineNearWidth) {
    s_laValid = true;
    s_laCount = count;
    s_laTwoDetected = twoDetected;
    s_laBottomLine = bottomLine;
    s_laCircleAngle = circleAngle;
    s_laBaseX = baseX;
    s_laBaseY = baseY;
    s_laBaseWidth = baseWidth;
    s_laTipX = tipX;
    s_laTipY = tipY;
    s_laTipWidth = tipWidth;
    s_laAngleDeg = angleDeg;
    s_laEncodedAngle = encodedAngle;
    s_laAvgY = avgY;
    s_laFlag = flag;
    s_laFineValid = fineValid;
    s_laFineAngleDeg = fineAngleDeg;
    s_laEncodedFineAngle = encodedFineAngle;
    s_laFineFarX = fineFarX;
    s_laFineFarY = fineFarY;
    s_laFineFarWidth = fineFarWidth;
    s_laFineNearX = fineNearX;
    s_laFineNearY = fineNearY;
    s_laFineNearWidth = fineNearWidth;
}

void xs_storeEvacTapeDebug(bool seen, const char* cls, float angleDeg,
                           uint8_t encodedAngle, uint16_t count,
                           uint16_t flashCount, uint16_t blackCount,
                           uint8_t minX, uint8_t minY,
                           uint8_t maxX, uint8_t maxY,
                           const uint8_t* maskX, const uint8_t* maskY,
                           uint8_t maskCount) {
    s_saValid = true;
    s_saSeen = seen;
    snprintf(s_saCls, sizeof(s_saCls), "%s", cls ? cls : "none");
    s_saAngleDeg = angleDeg;
    s_saEncodedAngle = encodedAngle;
    s_saCount = count;
    s_saFlashCount = flashCount;
    s_saBlackCount = blackCount;
    s_saMinX = minX;
    s_saMinY = minY;
    s_saMaxX = maxX;
    s_saMaxY = maxY;
    s_saMaskCount = maskCount > EVAC_MASK_MAX ? EVAC_MASK_MAX : maskCount;
    for (uint8_t i = 0; i < s_saMaskCount; i++) {
        s_saMaskX[i] = maskX ? maskX[i] : 0;
        s_saMaskY[i] = maskY ? maskY[i] : 0;
    }
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

int xs_formatLineAngleDebug(char* buf, int bufLen) {
    if (!s_laValid || bufLen < 180) return 0;
    return snprintf(buf, bufLen,
        "[LA] n=%u one=%u two=%u bottom=%u circ=%u angle=%.1f enc=%u y=%u flag=%u base=%d,%d bw=%u tip=%d,%d tw=%u fine=%u fang=%.1f fenc=%u far=%d,%d fw=%u near=%d,%d nw=%u\n",
        s_laCount,
        (s_laCount >= 1) ? 1 : 0,
        s_laTwoDetected ? 1 : 0,
        s_laBottomLine ? 1 : 0,
        s_laCircleAngle ? 1 : 0,
        s_laAngleDeg,
        s_laEncodedAngle,
        s_laAvgY,
        s_laFlag,
        s_laBaseX,
        s_laBaseY,
        s_laBaseWidth,
        s_laTipX,
        s_laTipY,
        s_laTipWidth,
        s_laFineValid ? 1 : 0,
        s_laFineAngleDeg,
        s_laEncodedFineAngle,
        s_laFineFarX,
        s_laFineFarY,
        s_laFineFarWidth,
        s_laFineNearX,
        s_laFineNearY,
        s_laFineNearWidth);
}

int xs_formatEvacTapeDebug(char* buf, int bufLen) {
    if (!s_saValid || bufLen < 32) return 0;
    if (!s_saSeen) {
        return snprintf(buf, bufLen, "[SA] seen=0 cls=none angle=0.0 enc=127\n");
    }

    int o = snprintf(buf, bufLen,
        "[SA] seen=1 cls=%s angle=%.1f enc=%u cnt=%u flash=%u black=%u box=%u,%u,%u,%u p=",
        s_saCls,
        s_saAngleDeg,
        s_saEncodedAngle,
        s_saCount,
        s_saFlashCount,
        s_saBlackCount,
        s_saMinX,
        s_saMinY,
        s_saMaxX,
        s_saMaxY);

    for (uint8_t i = 0; i < s_saMaskCount && o < bufLen - 12; i++) {
        o += snprintf(buf + o, bufLen - o, "%s%u,%u",
            (i ? " " : ""),
            s_saMaskX[i],
            s_saMaskY[i]);
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
