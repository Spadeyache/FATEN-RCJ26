#include "IMU.h"
#include "../../config.h"
#include "../drivers/yacheMPU6050.h"
#include <Wire.h>

namespace Sensors {
namespace IMU {

namespace {
    yacheMPU6050 _imu(Wire1);   // SCL1 = pin 17, SDA1 = pin 16
    float32_t    _pitch = 0.0f, _roll = 0.0f, _yaw = 0.0f;
}

void init() {
    _imu.begin();
    Serial.println("IMU ready.");
}

void tick() {
    _imu.update();
    _pitch = -_imu.getPitch();   // driver sign inverted vs robot convention
    _roll  =  _imu.getRoll();
    _yaw   =  _imu.getYaw();

#if PRINT_IMU
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 100) {
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
