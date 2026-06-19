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
}

void tick(bool instantRun) {
    // Line error, gap angle and commit flag are time-critical — update every loop.
    _lineError  = (float)Sensors::XIAO_link::get(XIAO_REG_COM);
    _gapAngle   = (float)Sensors::XIAO_link::get(XIAO_REG_ANGLE);
    _commitFlag = (Sensors::XIAO_link::get(XIAO_REG_FLAG) != 0);

    // Filter at 50 Hz unless caller requested an immediate update.
    static unsigned long lastFilter = 0;
    if (millis() - lastFilter >= 20 || instantRun) {
        const uint8_t raw = Sensors::XIAO_link::get(XIAO_REG_FEATURE);
        _command   = _filter.update(raw);
        lastFilter = millis();
    }
}

uint8_t command()    { return _command; }
float   lineError()  { return _lineError; }
float   gapAngle()   { return _gapAngle; }
bool    commitFlag() { return _commitFlag; }

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
