#include "YacheEncodedSerial.h"

YacheEncodedSerial::YacheEncodedSerial(HardwareSerial& serial) : _serial(&serial) {
    // 全てのIDの初期値を0にリセット
    for (int i = 0; i < 256; i++) _dataStorage[i] = 0;
    _dataStorage[2] = 127;
}

void YacheEncodedSerial::begin(unsigned long baud) {
    _serial->begin(baud);
}

void YacheEncodedSerial::send(uint8_t id, uint8_t value) {
    value = constrain(value, 0, 254); // 255を避ける
    _serial->write(_header);
    _serial->write(id);
    _serial->write(value);
}

void YacheEncodedSerial::update() {
    // 3バイト以上のセットがある限りループ
    while (_serial->available() >= 3) {
        // 同期処理：255を探す
        if (_serial->peek() != _header) {
            _serial->read();
            continue;
        }

        // 3バイト確定読み込み
        _serial->read(); // Header(255)
        uint8_t id = _serial->read();
        uint8_t val = _serial->read();

        // 届いたIDの箱に値を保存（これがキャッシュ方式）
        _dataStorage[id] = val;
    }
}

uint8_t YacheEncodedSerial::get(uint8_t id) {
    return _dataStorage[id];
}