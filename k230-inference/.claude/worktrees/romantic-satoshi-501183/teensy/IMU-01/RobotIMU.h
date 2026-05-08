#ifndef RobotIMU_h
#define RobotIMU_h

#include <Arduino.h>
#include <Wire.h>
#include <MadgwickAHRS.h>

class RobotIMU {
  public:
    // Constructor: allows passing the I2C port (defaults to Wire)
    RobotIMU(TwoWire &wirePort = Wire);

    void begin();
    void update(); // Call this in loop()

    // Getters for orientation
    float getRoll();
    float getPitch();
    float getHeading();

  private:
    TwoWire *_i2cPort; // Pointer to the I2C port
    Madgwick filter;
    
    unsigned long microsPerReading;
    unsigned long microsPrevious;
    
    // Internal helper functions
    void Ini_MPU6050();
    int Get_AX();
    int Get_AY();
    int Get_AZ();
    int Get_GX();
    int Get_GY();
    int Get_GZ();
    float convertRawAcceleration(int aRaw);
    float convertRawGyro(int gRaw);
};

#endif
