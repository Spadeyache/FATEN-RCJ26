#include "XIAO_link.h"
#include "../../config.h"
#include "../../pins_teensy.h"
#include "../drivers/yacheEncodedSerial.h"

namespace Sensors {
namespace XIAO_link {

namespace {
    YacheEncodedSerial _xiao(XIAO_SERIAL);
}

void init() {
    _xiao.begin(XIAO_BAUD);
}

void tick() {
    _xiao.update();
}

uint8_t get(uint8_t reg)                    { return _xiao.get(reg); }
void    send(uint8_t reg, uint8_t value)    { _xiao.send(reg, value); }

}  // namespace XIAO_link
}  // namespace Sensors
