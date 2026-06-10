#include "K230_link.h"
#include "../../config.h"

namespace Sensors {
namespace K230_link {

namespace {
    uint8_t       _cmd        = 0x00;       // 0x00 idle, 0x01 detect
    unsigned long _last_cmd_ms = 0;
}

void init() {
    Serial8.begin(K230_BAUD);
    // Teensy-side K230 run/idle control disabled for now.
    // _cmd = 0x00;       // setup default: K230 not detecting/rest
    _last_cmd_ms = 0;
    // Serial8.write(_cmd);
}

void tick() {
    // Outbound: periodic command byte.
    // Teensy-side K230 run/idle control disabled for now.
    // if (millis() - _last_cmd_ms >= K230_CMD_INTERVAL) {
    //     Serial8.write(_cmd);
    //     _last_cmd_ms = millis();
    // }
    // Inbound: Serial8 already buffers; readByte() drains it on demand.
}

int readByte() {
    return Serial8.available() ? Serial8.read() : -1;
}

void setCommand(uint8_t cmd) {
    // Teensy-side K230 run/idle control disabled for now.
    // _cmd = cmd;
    (void)cmd;
}

}  // namespace K230_link
}  // namespace Sensors
