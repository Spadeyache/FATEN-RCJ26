#include "XiaoDecode.h"
#include "../sensors/XIAO_link.h"
#include <Arduino.h>

namespace Processing {
namespace XiaoDecode {

namespace {
    CommandFilter _filter;
    uint8_t       _command    = 0;
    float         _lineError  = 127.0f;
    bool          _commitFlag = false;
    bool          _tightSlowFlag = false;
    bool          _bottomLineFlag = false;
}

void tick(bool instantRun) {
    _lineError = (float)Sensors::XIAO_link::get(XIAO_REG_COM);

    const uint8_t flags = Sensors::XIAO_link::get(XIAO_REG_FLAG);
    _commitFlag = (flags & XIAO_FLAG_COMMIT) != 0;
    _tightSlowFlag = (flags & XIAO_FLAG_TIGHT_SLOW) != 0;
    _bottomLineFlag = (flags & XIAO_FLAG_BOTTOM_LINE) != 0;

    static unsigned long lastFilter = 0;
    if (millis() - lastFilter >= 20 || instantRun) {
        const uint8_t raw = Sensors::XIAO_link::get(XIAO_REG_FEATURE);
        _command = _filter.update(raw);
        lastFilter = millis();
    }
}

uint8_t command()          { return _command; }
float   lineError()        { return _lineError; }
bool    commitFlag()       { return _commitFlag; }
bool    tightSlowFlag()    { return _tightSlowFlag; }
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
