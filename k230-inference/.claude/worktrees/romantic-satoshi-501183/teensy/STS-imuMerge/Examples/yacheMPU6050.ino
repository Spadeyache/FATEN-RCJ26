#include "yacheMPU6050.h"

yacheMPU6050 imu;

void setup() {
    Serial.begin(115200);
    
    imu.begin(Wire, 100.0f);



    // imu.calibrate(); 
    imu.loadOffsetsFromEEPROM(); 
    Serial.println("IMU Ready.");
}

void loop() {
    imu.update();

    // Standard output
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 50) {
        Serial.printf("Pitch: %.2f | Roll: %.2f | Yaw: %.2f\n", 
                      imu.getPitch(), imu.getRoll(), imu.getYaw());
        lastPrint = millis();
    }

    // printQuat();
}
