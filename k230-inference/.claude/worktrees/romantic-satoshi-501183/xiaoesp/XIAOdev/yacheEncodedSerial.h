// SHARED PROTOCOL: duplicate also lives in teensy4.1/STS-imuMerge/ — keep both copies in sync
#ifndef YACHE_ENCODED_SERIAL_H
#define YACHE_ENCODED_SERIAL_H

#include <Arduino.h>

class YacheEncodedSerial {
public:
    YacheEncodedSerial(HardwareSerial& serial);
    void begin(unsigned long baud);

    // バッファにある全てのパケットを読み込み、内部配列を更新する
    void update();

    // 指定したIDの最新値を返す
    uint8_t get(uint8_t id);

    // [255, id, value] を送信する
    void send(uint8_t id, uint8_t value);

private:
    HardwareSerial* _serial;
    const uint8_t _header = 255;
    uint8_t _dataStorage[256]; // IDごとのデータ保管庫
};

#endif