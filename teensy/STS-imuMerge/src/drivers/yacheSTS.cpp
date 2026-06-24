#include "yacheSTS.h"

yacheSTS::yacheSTS() {}

FLASHMEM void yacheSTS::begin(HardwareSerial &serialPort, unsigned long baud) {
    serialPort.begin(baud);
    _sts.pSerial = &serialPort;
}

void yacheSTS::setWheelMode() {
    for (int i = 0; i < 4; i++) _sts.WheelMode(_ids[i]);
}

void yacheSTS::setWheelMode(bool enable) {
    stop();
    delay(10);
    for (int i = 0; i < 4; i++) {
        if (enable) _sts.WheelMode(_ids[i]);
        else        _sts.ServoMode(_ids[i]);
    }
}

// FASTRUN moves this function to ITCM RAM (600 MHz zero-wait-state).
FASTRUN void yacheSTS::power(float32_t lf, float32_t rf, float32_t lb, float32_t rb) {
    float32_t inputs[4] = {lf, rf, lb, rb};

    for (int i = 0; i < 4; i++) {
        inputs[i] = fmaxf(-100.0f, fminf(inputs[i], 100.0f));
        // Mapping: 100.0f -> 4000 (servo's full-speed register value).
        // Per-wheel sign from pins_teensy.h (STS_INVERT_*) so a positive arg
        // always drives that wheel forward regardless of mounting/wiring.
        _speeds[i] = (int16_t)(inputs[i] * 40.0f * _invert[i]);
    }

    _sts.SyncWriteSpe(_ids, 4, _speeds, _accs);
}

FASTRUN void yacheSTS::stop() {
    power(0, 0, 0, 0);
}
