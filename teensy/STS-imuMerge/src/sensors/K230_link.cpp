#include "K230_link.h"
#include "config.h"

namespace Sensors {
namespace K230_link {

namespace {
    uint8_t       _cmd        = 0x00;       // 0x00 idle, 0x01 detect
    unsigned long _last_cmd_ms = 0;
}

void init() {
    Serial5.begin(K230_BAUD);
}

void tick() {
    // Outbound: periodic command byte.
    if (millis() - _last_cmd_ms >= K230_CMD_INTERVAL) {
        Serial5.write(_cmd);
        _last_cmd_ms = millis();
    }
    // Inbound: Serial5 already buffers; readByte() drains it on demand.
}

int readByte() {
    return Serial5.available() ? Serial5.read() : -1;
}

void setCommand(uint8_t cmd) {
    _cmd = cmd;
}

}  // namespace K230_link
}  // namespace Sensors
