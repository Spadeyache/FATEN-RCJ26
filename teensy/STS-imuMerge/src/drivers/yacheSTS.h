#pragma once

// =============================================================================
//  yacheSTS — 4-wheel Feetech STS smart-servo driver
//  Talks SyncWriteSpe over a HardwareSerial UART (default 1 Mbps).
//  IDs are hardcoded to {4, 1, 2, 3} (FL, FR, BL, BR) — see _ids[] below.
// =============================================================================

#include <Arduino.h>
#include <SCServo.h>
#include <arm_math.h>
#include "../../pins_teensy.h"

class yacheSTS {
    public:
        yacheSTS();

        void begin(HardwareSerial &serialPort, unsigned long baud = 1000000) FLASHMEM;
        void setWheelMode();
        void setWheelMode(bool enable);
        // power() ranges: each arg is -100..+100. Right-side signs are inverted internally.
        void power(float32_t lf, float32_t rf, float32_t lb, float32_t rb) FASTRUN;
        void stop() FASTRUN;

    private:
        SMS_STS _sts;
        uint8_t _ids[4]    = {STS_ID_FL, STS_ID_FR, STS_ID_BL, STS_ID_BR};   // FL, FR, BL, BR
        int8_t  _invert[4] = {STS_INVERT_FL, STS_INVERT_FR, STS_INVERT_BL, STS_INVERT_BR};
        int16_t _speeds[4] = {0, 0, 0, 0};
        uint8_t _accs[4]   = {0, 0, 0, 0};
};
