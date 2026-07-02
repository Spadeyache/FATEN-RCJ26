#include "XiaoDecode.h"
#include "../sensors/XIAO_link.h"
#include <Arduino.h>

namespace Processing {
namespace XiaoDecode {

namespace {
    CommandFilter _filter;
    uint8_t       _command    = 0;
    float         _lineError  = 127.0f;
    float         _gapAngle   = 127.0f;
    float         _gapFineAngle = 127.0f;
    bool          _commitFlag = false;
    bool          _tightSlowFlag = false;
    bool          _bottomLineFlag = false;
    bool          _sideLineFlag = false;
    bool          _fineAngleFlag = false;
}

void tick(bool instantRun) {
    _lineError = (float)Sensors::XIAO_link::get(XIAO_REG_COM);
    _gapAngle = (float)Sensors::XIAO_link::get(XIAO_REG_ANGLE);
    _gapFineAngle = (float)Sensors::XIAO_link::get(XIAO_REG_FINE_ANGLE);

    const uint8_t flags = Sensors::XIAO_link::get(XIAO_REG_FLAG);
    _commitFlag = (flags & XIAO_FLAG_COMMIT) != 0;
    _tightSlowFlag = (flags & XIAO_FLAG_TIGHT_SLOW) != 0;
    _bottomLineFlag = (flags & XIAO_FLAG_BOTTOM_LINE) != 0;
    _sideLineFlag = (flags & XIAO_FLAG_SIDE_LINE) != 0;
    _fineAngleFlag = (flags & XIAO_FLAG_FINE_ANGLE) != 0;

    static unsigned long lastFilter = 0;
    if (millis() - lastFilter >= 20 || instantRun) {
        const uint8_t raw = Sensors::XIAO_link::get(XIAO_REG_FEATURE);
        _command = _filter.update(raw);
        lastFilter = millis();
    }
}

uint8_t command()          { return _command; }
float   lineError()        { return _lineError; }
float   gapAngle()         { return _gapAngle; }
float   gapFineAngle()     { return _gapFineAngle; }
uint8_t gapLineY()         { return (uint8_t)(_lineError + 0.5f); }
uint8_t gapLineCount()     { return gapLineY(); }
bool    commitFlag()       { return _commitFlag; }
bool    tightSlowFlag()    { return _tightSlowFlag; }
bool    gapAnyPointFlag()  { return _commitFlag; }
bool    gapBothRowsFlag()  { return _tightSlowFlag; }
bool    gapBottomLineFlag(){ return _bottomLineFlag; }
bool    gapSideLineFlag()  { return _sideLineFlag; }
bool    gapFineAngleFlag() { return _fineAngleFlag; }
bool    obstacleSeeLine()  { return _commitFlag; }
float   obstacleAngle()    { return _gapAngle; }
bool    silverSeen()       { return _commitFlag; }
bool    evacBlackSeen()    { return _bottomLineFlag; }

void setMode(XiaoMode m) {
    Sensors::XIAO_link::send(XIAO_REG_MODE, (uint8_t)m);
}

void clearFilter() {
    _filter.clear();
    _command = 0;
}

void setCommand(uint8_t c) { _command = c; }

const CommandFilter& filter() { return _filter; }

}  // namespace XiaoDecode
}  // namespace Processing
