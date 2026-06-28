#pragma once

#include <Arduino.h>
#include "../processing/LineCount.h"

// PC-facing stream/debug state for esp32_camera_viewer.html.
//
// The vision modes own detection and Teensy decisions. This module owns only
// what the PC viewer should draw: line-follow geometry, crossing points, and
// the left/right color-sensor display.

enum XiaoStreamSide : uint8_t {
    XS_LEFT  = 0,
    XS_RIGHT = 1
};

enum XiaoStreamClass : uint8_t {
    XS_WHITE  = ROW55_WHITE,
    XS_GREEN  = ROW55_GREEN,
    XS_RED    = ROW55_RED,
    XS_SILVER = ROW55_SILVER,
    XS_BLACK  = ROW55_BLACK
};

enum XiaoStreamPriority : uint8_t {
    XS_PRIO_NONE   = 0,
    XS_PRIO_LINE   = 1,
    XS_PRIO_GAP    = 2,
    XS_PRIO_GREEN  = 3,
    XS_PRIO_RED    = 4,
    XS_PRIO_SILVER = 5
};

void xs_beginSensorFrame();
void xs_setSensorBoth(XiaoStreamClass cls, XiaoStreamPriority prio);
void xs_setSensorSide(XiaoStreamSide side, XiaoStreamClass cls, XiaoStreamPriority prio);

void xs_storeLineDebug(const LineCounts& lc, const LineClass& cls, int focusedOut,
                       int errByte, float errPx);
void xs_storeLineSteer(int steerOut, bool commitActive, bool commitLocked,
                       int commitProgress, uint8_t greenCmd,
                       uint16_t arcBlackCount, bool arcBlackSaturated);

void xs_noteCommitStart(bool left);
void xs_noteCommitLock(bool left, uint8_t x, uint8_t y);
void xs_noteCommitEnd(const char* reason);

void xs_storeLineAngleDebug(uint8_t count, bool twoDetected, bool bottomLine,
                            bool circleAngle, int baseX, int baseY,
                            uint8_t baseWidth, int tipX, int tipY,
                            uint8_t tipWidth, float angleDeg,
                            uint8_t encodedAngle, uint8_t avgY,
                            uint8_t flag, bool fineValid,
                            float fineAngleDeg, uint8_t encodedFineAngle,
                            int fineFarX, int fineFarY, uint8_t fineFarWidth,
                            int fineNearX, int fineNearY, uint8_t fineNearWidth);

void xs_storeEvacTapeDebug(bool seen, const char* cls, float angleDeg,
                           uint8_t encodedAngle, uint16_t count,
                           uint16_t flashCount, uint16_t blackCount,
                           uint8_t minX, uint8_t minY,
                           uint8_t maxX, uint8_t maxY,
                           const uint8_t* maskX, const uint8_t* maskY,
                           uint8_t maskCount);

int xs_formatLineDebug(char* buf, int bufLen);
int xs_formatLineAngleDebug(char* buf, int bufLen);
int xs_formatEvacTapeDebug(char* buf, int bufLen);
int xs_formatSensorRow(char* buf, int bufLen);
int xs_formatEvent(char* buf, int bufLen);
