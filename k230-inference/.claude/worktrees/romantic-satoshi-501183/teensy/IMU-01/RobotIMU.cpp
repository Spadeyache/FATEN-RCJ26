#include "RobotIMU.h"

RobotIMU::RobotIMU(TwoWire &wirePort) {
  _i2cPort = &wirePort;
}

void RobotIMU::begin() {
  _i2cPort->begin();
  // Optional: _i2cPort->setClock(400000); // Increase I2C speed to 400kHz if needed

  Ini_MPU6050();

  // initialize variables to pace updates to correct rate
  filter.begin(25); // Initialize Madgwick filter with sample frequency (25Hz)
  microsPerReading = 1000000 / 25;
  microsPrevious = micros();
}

void RobotIMU::update() {
  unsigned long microsNow = micros();
  if (microsNow - microsPrevious >= microsPerReading) {
    
    // Convert from raw data to gravity and degrees/second units
    float ax = convertRawAcceleration(Get_AX());
    float ay = convertRawAcceleration(Get_AY());
    float az = convertRawAcceleration(Get_AZ());
    float gx = convertRawGyro(Get_GX());
    float gy = convertRawGyro(Get_GY());
    float gz = convertRawGyro(Get_GZ());

    // Update the filter
    filter.updateIMU(gx, gy, gz, ax, ay, az);

    // increment previous time, so we keep proper pace
    microsPrevious = microsPrevious + microsPerReading;
  }
}

float RobotIMU::getRoll() {
  return filter.getRoll();
}

float RobotIMU::getPitch() {
  return filter.getPitch();
}

float RobotIMU::getHeading() {
  return filter.getYaw();
}

// --- Private Helper Functions ---

void RobotIMU::Ini_MPU6050() {
  _i2cPort->beginTransmission(0x68);             // MPU6050 Select
  _i2cPort->write(0x6B);                   // MPU6050_PWR_MGMT_1 register address
  _i2cPort->write(0x00);                   // Write 0 to start sensor
  _i2cPort->endTransmission();                 // End transmission

  _i2cPort->beginTransmission(0x68);             // LPF setting
  _i2cPort->write(0x1A);                   // External sync, LPF setting
  _i2cPort->write(0x06);                   // --,000(ext sync disabled),110(LPF max effect)
  _i2cPort->endTransmission();                 

  _i2cPort->beginTransmission(0x68);             // Gyro init
  _i2cPort->write(0x1B);                   // Gyro 3-axis self test, full scale
  _i2cPort->write(0x18);                   // 000(self test disabled),xx(full scale),---
  _i2cPort->endTransmission();                 // 18:2000deg/s

  _i2cPort->beginTransmission(0x68);             // Accel init
  _i2cPort->write(0x1C);                   // Accel 3-axis self test, full scale
  _i2cPort->write(0x00);                   // 000(self test disabled),xx(full scale),---
  _i2cPort->endTransmission();                 // 00:2G
}

int RobotIMU::Get_AX() {
  _i2cPort->beginTransmission(0x68);
  _i2cPort->write(0x3B);
  _i2cPort->endTransmission(false);
  _i2cPort->requestFrom(0x68, 2);
  while (_i2cPort->available() < 2);
  return _i2cPort->read() << 8 | _i2cPort->read();
}

int RobotIMU::Get_AY() {
  _i2cPort->beginTransmission(0x68);
  _i2cPort->write(0x3D);
  _i2cPort->endTransmission(false);
  _i2cPort->requestFrom(0x68, 2);
  while (_i2cPort->available() < 2);
  return _i2cPort->read() << 8 | _i2cPort->read();
}

int RobotIMU::Get_AZ() {
  _i2cPort->beginTransmission(0x68);
  _i2cPort->write(0x3F);
  _i2cPort->endTransmission(false);
  _i2cPort->requestFrom(0x68, 2);
  while (_i2cPort->available() < 2);
  return _i2cPort->read() << 8 | _i2cPort->read();
}

int RobotIMU::Get_GX() {
  _i2cPort->beginTransmission(0x68);
  _i2cPort->write(0x43);
  _i2cPort->endTransmission(false);
  _i2cPort->requestFrom(0x68, 2);
  while (_i2cPort->available() < 2);
  return _i2cPort->read() << 8 | _i2cPort->read();
}

int RobotIMU::Get_GY() {
  _i2cPort->beginTransmission(0x68);
  _i2cPort->write(0x45);
  _i2cPort->endTransmission(false);
  _i2cPort->requestFrom(0x68, 2);
  while (_i2cPort->available() < 2);
  return _i2cPort->read() << 8 | _i2cPort->read();
}

int RobotIMU::Get_GZ() {
  _i2cPort->beginTransmission(0x68);
  _i2cPort->write(0x47);
  _i2cPort->endTransmission(false);
  _i2cPort->requestFrom(0x68, 2);
  while (_i2cPort->available() < 2);
  return _i2cPort->read() << 8 | _i2cPort->read();
}

float RobotIMU::convertRawAcceleration(int aRaw) {
  // since we are using 2 g range
  // -2 g maps to a raw value of -32768
  // +2 g maps to a raw value of 32767
  return (aRaw * 2.0) / 32768.0;
}

float RobotIMU::convertRawGyro(int gRaw) {
  // since we are using 250 degrees/seconds range
  // -250 maps to a raw value of -32768
  // +250 maps to a raw value of 32767
  return (gRaw * 250.0) / 32768.0;
}
