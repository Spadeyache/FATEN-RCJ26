#include "yacheEncodedSerial.h"

YacheEncodedSerial::YacheEncodedSerial(HardwareSerial& serial) : _serial(&serial) {
    for (int i = 0; i < 256; i++) _dataStorage[i] = 0;
    _dataStorage[2] = 127;   // XIAO_REG_COM defaults to "centred"
}

void YacheEncodedSerial::begin(unsigned long baud) {
    _serial->begin(baud);
}

void YacheEncodedSerial::send(uint8_t id, uint8_t value) {
    value = constrain(value, 0, 254);   // avoid 255 (reserved header)
    _serial->write(_header);
    _serial->write(id);
    _serial->write(value);
}

void YacheEncodedSerial::update() {
    while (_serial->available() >= 3) {
        if (_serial->peek() != _header) {
            _serial->read();   // re-sync: drop until header byte
            continue;
        }
        _serial->read();                          // consume header
        uint8_t id  = _serial->read();
        uint8_t val = _serial->read();
        _dataStorage[id] = val;
    }
}

uint8_t YacheEncodedSerial::get(uint8_t id) {
    return _dataStorage[id];
}
