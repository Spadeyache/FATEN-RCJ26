#pragma once

// =============================================================================
//  yacheEncodedSerial — 3-byte [255, id, value] register protocol over UART.
//
//  Header byte 255 is reserved (values are clamped to 0..254 on send so the
//  header is always unambiguous).
//
//  SHARED PROTOCOL: a duplicate of this driver lives in xiaoesp32/XIAOdev/.
//  Keep both copies in sync. Register IDs are defined in config.h
//  (XIAO_REG_*).
// =============================================================================

#include <Arduino.h>

class YacheEncodedSerial {
public:
    YacheEncodedSerial(HardwareSerial& serial);
    void begin(unsigned long baud);

    // Drain the UART RX buffer, updating the per-ID register cache.
    void update();

    // Latest value for register `id` (0 if never received).
    uint8_t get(uint8_t id);

    // Transmit [255, id, value]. Value is clamped to 0..254.
    void send(uint8_t id, uint8_t value);

private:
    HardwareSerial* _serial;
    const uint8_t   _header = 255;
    uint8_t         _dataStorage[256];
};
