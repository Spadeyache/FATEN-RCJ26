#ifndef yacheSTS_h
#define yacheSTS_h

#include <Arduino.h>
#include <SCServo.h>
#include <arm_math.h>

class yacheSTS {
    public:
        yacheSTS();

        // STS motor can be initialized as name.begin(dklfj); 
        // delay(500);
        // setWHeelMode();
        void begin(HardwareSerial &serialPort, unsigned long baud = 1000000) FLASHMEM;
        void setWheelMode();
        void setWheelMode(bool enable);
        // Optimization: FASTRUN ensures this executes from RAM
        void power(float32_t lf, float32_t rf, float32_t lb, float32_t rb) FASTRUN;
        void stop() FASTRUN;

    private:
        SMS_STS _sts;
        uint8_t _ids[4] = {4, 1, 2, 3};
        int16_t _speeds[4] = {0, 0, 0, 0};   // Must be int16_t (s16)
        uint8_t _accs[4] = {0, 0, 0, 0}; //150
};

#endif


