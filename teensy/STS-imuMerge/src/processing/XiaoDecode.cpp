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
    bool          _commitFlag = false;
    bool          _tightSlowFlag = false;
}

void tick(bool instantRun) {
    // Line error, gap angle and commit flag are time-critical — update every loop.
    _lineError  = (float)Sensors::XIAO_link::get(XIAO_REG_COM);
    _gapAngle   = (float)Sensors::XIAO_link::get(XIAO_REG_ANGLE);
    const uint8_t flags = Sensors::XIAO_link::get(XIAO_REG_FLAG);
    _commitFlag = (flags & XIAO_FLAG_COMMIT) != 0;
    _tightSlowFlag = (flags & XIAO_FLAG_TIGHT_SLOW) != 0;

    // Filter at 50 Hz unless caller requested an immediate update.
    static unsigned long lastFilter = 0;
    if (millis() - lastFilter >= 20 || instantRun) {
        const uint8_t raw = Sensors::XIAO_link::get(XIAO_REG_FEATURE);
        _command   = _filter.update(raw);
        lastFilter = millis();
    }
}

uint8_t command()          { return _command; }
float   lineError()        { return _lineError; }
float   gapAngle()         { return _gapAngle; }
uint8_t gapLineCount()     { return (uint8_t)(_lineError + 0.5f); }  // COM carries count in LINE_ANGLE mode
bool    commitFlag()       { return _commitFlag; }
bool    tightSlowFlag()    { return _tightSlowFlag; }
bool    gapBothRowsFlag()  { return _commitFlag; }  // XIAO_FLAG_COMMIT bit reused for both-rows flag
bool    obstacleSeeLine()  { return _commitFlag; }  // XIAO_FLAG_COMMIT bit reused for see-line flag
float   obstacleAngle()    { return _gapAngle; }    // ANGLE register reused for line tilt
bool    silverSeen()       { return _commitFlag; }  // XIAO_FLAG_COMMIT bit reused for silver-seen flag
float   silverAlignAngle() { return _gapAngle; }    // ANGLE register reused for tape tilt

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
