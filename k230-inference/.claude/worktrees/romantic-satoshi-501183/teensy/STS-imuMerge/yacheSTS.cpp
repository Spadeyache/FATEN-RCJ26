#include "yacheSTS.h"

yacheSTS::yacheSTS() {}

FLASHMEM void yacheSTS::begin(HardwareSerial &serialPort, unsigned long baud) {
    serialPort.begin(baud);
    _sts.pSerial = &serialPort;
//     // delay(500); // Wait for servos to boot
}

void yacheSTS::setWheelMode() {
    for(int i = 0; i < 4; i++) {
        _sts.WheelMode(_ids[i]);
    }
}

void yacheSTS::setWheelMode(bool enable) {
    stop(); 
    delay(10); 
    for(int i = 0; i < 4; i++) {
        if(enable) _sts.WheelMode(_ids[i]);
        else _sts.ServoMode(_ids[i]);
    }
}

// FASTRUN moves this function to ITCM RAM (600MHz zero-wait-state)
FASTRUN void yacheSTS::power(float32_t lf, float32_t rf, float32_t lb, float32_t rb) {
    float32_t inputs[4] = {lf, rf, lb, rb};

    for(int i = 0; i < 4; i++) {
        // Hardware-level clipping (VMAX.F32 / VMIN.F32 instructions)
        inputs[i] = fmaxf(-100.0f, fminf(inputs[i], 100.0f));
    }

    // Mapping: 100.0f -> 4000
    // Using 'f' suffix ensures 32-bit FPU usage instead of 64-bit software double
    // Inverting rf and rb
    _speeds[0] = (int16_t)(inputs[0] * 40.0f);
    _speeds[1] = (int16_t)(inputs[1] * -40.0f); 
    _speeds[2] = (int16_t)(inputs[2] * 40.0f);
    _speeds[3] = (int16_t)(inputs[3] * -40.0f);

    _sts.SyncWriteSpe(_ids, 4, _speeds, _accs);
}

FASTRUN void yacheSTS::stop() {
    power(0, 0, 0, 0);
}



