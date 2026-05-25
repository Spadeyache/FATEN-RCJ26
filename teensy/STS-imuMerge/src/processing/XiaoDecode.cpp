#include "XiaoDecode.h"
#include "../sensors/XIAO_link.h"
#include <Arduino.h>

namespace Processing {
namespace XiaoDecode {

namespace {
    CommandFilter _filter;
    uint8_t       _command   = 0;
    float         _lineError = 127.0f;
    float         _gapAngle  = 127.0f;
}

void tick(bool instantRun) {
    // Line error and gap angle are time-critical — update every loop.
    _lineError = (float)Sensors::XIAO_link::get(XIAO_REG_COM);
    _gapAngle  = (float)Sensors::XIAO_link::get(XIAO_REG_ANGLE);

    // Filter at 50 Hz unless caller requested an immediate update.
    static unsigned long lastFilter = 0;
    if (millis() - lastFilter >= 20 || instantRun) {
        const uint8_t raw = Sensors::XIAO_link::get(XIAO_REG_FEATURE);
        _command   = _filter.update(raw);
        lastFilter = millis();
    }
}

uint8_t command()   { return _command; }
float   lineError() { return _lineError; }
float   gapAngle()  { return _gapAngle; }

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
