#include "IMU.h"
#include "../../config.h"
#include "../../pins_teensy.h"
#include "../drivers/yacheMPU6050.h"
#include <Arduino.h>
#include <Wire.h>

namespace Sensors {
namespace IMU {

namespace {
    yacheMPU6050 _imu(IMU_WIRE);   // SCL1 = pin 17, SDA1 = pin 16
    IntervalTimer _timer;
    // Written from the timer ISR, read from the main loop. Aligned float
    // read/write is a single atomic instruction on Cortex-M, so plain
    // volatile (no lock) is enough here.
    volatile float32_t _pitch = 0.0f, _roll = 0.0f, _yaw = 0.0f;

    // Runs at IMU_SAMPLE_HZ regardless of what the main loop is doing
    // (delay()s included). Kept tiny + FASTRUN so it can't itself stall
    // higher-priority interrupts for long.
    FASTRUN void isr() {
        _imu.update();
        _pitch = _imu.getPitch();
        _roll  = _imu.getRoll();
        _yaw   = _imu.getYaw();
    }
}

void init() {
    _imu.begin();   // synchronous: calibration/EEPROM finish before the timer starts

    _timer.begin(isr, (uint32_t)(1000000.0f / IMU_SAMPLE_HZ));
    // Lowest NVIC priority: lets the I2C peripheral's own interrupt (which the
    // blocking Wire calls inside update() wait on) preempt this ISR instead of
    // deadlocking the bus.
    _timer.priority(255);

    Serial.println("IMU ready.");
}

void tick() {
    // Sampling is driven by the ISR now; this is just the optional debug print.
#if PRINT_IMU
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 40) {   // ~25 Hz, matches IMU-01's per-loop print cadence
        Serial.printf("pitch:%.2f roll:%.2f yaw:%.2f\n",
                      (float)_pitch, (float)_roll, (float)_yaw);
        lastPrint = millis();
    }
#endif
}

float32_t getPitch() { return _pitch; }
float32_t getRoll()  { return _roll;  }
float32_t getYaw()   { return _yaw;   }

}  // namespace IMU
}  // namespace Sensors
