/*
2/21/2026
Took the code from MPU6050-MotorGain-zaxis3.ino

A code that prints the pitch row and yaw for MPU6050 where the sensor -x axis is the gravitational vector.

*/


#include "Wire.h"
#include "I2Cdev.h"
#include "MPU6050.h"
#include <MadgwickAHRS.h>
#include <EEPROM.h>

MPU6050 mpu;
Madgwick filter;

unsigned long microsPerReading, microsPrevious;
int16_t axRaw, ayRaw, azRaw, gxRaw, gyRaw, gzRaw;
float ax, ay, az;
float gx, gy, gz;
float pitch, roll, yaw;

int buffersize = 1000;
int acel_deadzone = 8;
int giro_deadzone = 1;

int mean_ax, mean_ay, mean_az, mean_gx, mean_gy, mean_gz;
int ax_offset = -4737;
int ay_offset = -374;
int az_offset = 631;
int gx_offset = 19;
int gy_offset = 54;
int gz_offset = 2;

//int ax_offset = -278;
//int ay_offset = 540;
//int az_offset = 2420;
//int gx_offset = 64;
//int gy_offset = 16;
//int gz_offset = -6;


void setup() {
  Serial.begin(9600);
  Wire.begin();

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed");
    while (1);
  }

  // Run calibration (comment out after initial calibration if you want to hardcode offsets)
  mpuCalib();

  oldIMUoffset();//Data from EEPROM
  
  // Apply calibrated offsets
  mpu.setXAccelOffset(ax_offset);
  mpu.setYAccelOffset(ay_offset);
  mpu.setZAccelOffset(az_offset);
  mpu.setXGyroOffset(gx_offset);
  mpu.setYGyroOffset(gy_offset);
  mpu.setZGyroOffset(gz_offset);

  // Initialize Madgwick filter
  filter.begin(25);
  microsPerReading = 1000000 / 25;
  microsPrevious = micros();
}

void loop() {
  
  IMUupdate();
  Serial.print("Orientation (Yaw, Pitch, Roll): ");
  Serial.print(yaw); Serial.print(" ");
  Serial.print(pitch); Serial.print(" ");
  Serial.println(roll);
  
}

// === Motor ===
const int axisOffgain0 = 1;
const int axisOffgain1 = 1;
const int axisOffgain2 = 1;
const int axisOffgain3 = 1;

const int motorOffset0 = 0;
const int motorOffset1 = 0;
const int motorOffset2 = 0;
const int motorOffset3 = 0;

const int motorSlope0 = 0;
const int motorSlope1 = 0;
const int motorSlope2 = 0;
const int motorSlope3 = 0;

const int weightInversePitch[] = {690, 680, 670, 660, 650, 640, 630, 620, 610, 600, 590, 580, 570, 560, 550, 540, 530, 520, 510, 500, 490, 480, 470, 460, 450, 440, 430, 420, 410, 400, 390, 380, 370, 360, 350, 340, 330, 320, 310, 300, 290, 280, 270, 260, 250, 240, 230, 220, 210, 200, 190, 180, 170, 160, 150, 140, 130, 120, 110, 100, 90, 80, 70, 60, 50, 40, 3, 2, 1}; //Avr Left weight distribution when we tilt the robot in pitch direction 
const int weightInverseRoll[] = {1, 2, 3, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 250, 260, 270, 280, 290, 300, 310, 320, 330, 340, 350, 360, 370, 380, 390, 400, 410, 420, 430, 440, 450, 460, 470, 480, 490, 500, 510, 520, 530, 540, 550, 560, 570, 580, 590, 600, 610, 620, 630, 640, 650, 660, 670, 680, 690}; //Front weight distribution when we tilt the robot in roll direction
/* find these const by measuring with a weight and subtracting the */

void motor(int left, int right) { 
  /*
   * based on left and right, it convroles the motor with the left and right values.
   * The Left and right values -100 to 100.
   * Kent will add an code that adjust the motor power based on the roll and pitch
   */
}


// === EEPROM ===

void oldIMUoffset() {
  EEPROM.get(0, ax_offset);
  EEPROM.get(4, ay_offset);
  EEPROM.get(8, az_offset);
  EEPROM.get(12, gx_offset);
  EEPROM.get(16, gy_offset);
  EEPROM.get(20, gz_offset);

  Serial.print("int ax_offset = "); Serial.print(ax_offset); Serial.println(";");
  Serial.print("int ay_offset = "); Serial.print(ay_offset); Serial.println(";");
  Serial.print("int az_offset = "); Serial.print(az_offset); Serial.println(";");
  Serial.print("int gx_offset = "); Serial.print(gx_offset); Serial.println(";");
  Serial.print("int gy_offset = "); Serial.print(gy_offset); Serial.println(";");
  Serial.print("int gz_offset = "); Serial.print(gz_offset); Serial.println(";");
}

// === IMU ===

float convertRawAcceleration(int16_t aRaw) {
  return ((float)aRaw) / 16384.0; // ±2g
}

float convertRawGyro(int16_t gRaw) {
  return ((float)gRaw) / 131.0; // ±250°/s
}

void IMUupdate() {
  unsigned long microsNow = micros();

  if (microsNow - microsPrevious >= microsPerReading) {
    mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);

    ax = convertRawAcceleration(axRaw);
    ay = convertRawAcceleration(ayRaw);
    az = convertRawAcceleration(azRaw);
    gx = convertRawGyro(gxRaw);
    gy = convertRawGyro(gyRaw);
    gz = convertRawGyro(gzRaw);

//    filter.updateIMU(gz, gy, gx, az, ay, ax);
    filter.updateIMU(gy, gz, gx, ay, az, ax);

    roll = filter.getRoll();
    pitch = filter.getPitch();
    yaw = filter.getYaw();

    roll = int(roll);//for simple computation
    pitch = int(pitch);//for simple computation

    microsPrevious += microsPerReading;
  }
}

// === Calibration ===

void mpuCalib() {
  Serial.println("Starting calibration...");

  mpu.setXAccelOffset(0);
  mpu.setYAccelOffset(0);
  mpu.setZAccelOffset(0);
  mpu.setXGyroOffset(0);
  mpu.setYGyroOffset(0);
  mpu.setZGyroOffset(0);
  delay(1000);

  Serial.println("Calibration In Prog...");

  meansensors();
  Serial.println("Calibration In Prog...");
  calibration();
  Serial.println("Calibration In Prog...");

  meansensors();

  Serial.println("\nFINISHED!");
  Serial.println("Use these defines in your code:");
  Serial.print("int ax_offset = "); Serial.print(ax_offset);Serial.println(";");
  Serial.print("int ay_offset = "); Serial.print(ay_offset);Serial.println(";");
  Serial.print("int az_offset = "); Serial.print(az_offset);Serial.println(";");
  Serial.print("int gx_offset = ");  Serial.print(gx_offset);Serial.println(";");
  Serial.print("int gy_offset = ");  Serial.print(gy_offset);Serial.println(";");
  Serial.print("int gz_offset = ");  Serial.print(gz_offset);Serial.println(";");

  Serial.println("Saving Data...");
  save(); //save to EEPROM
  delay(600);
}

void meansensors() {
  long i = 0, buff_ax = 0, buff_ay = 0, buff_az = 0;
  long buff_gx = 0, buff_gy = 0, buff_gz = 0;

  for (i = 0; i < (buffersize + 100); i++) {
    mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);
    if (i > 100) {
      buff_ax += axRaw; buff_ay += ayRaw; buff_az += azRaw;
      buff_gx += gxRaw; buff_gy += gyRaw; buff_gz += gzRaw;
    }
    delay(2);
  }

  mean_ax = buff_ax / buffersize;
  mean_ay = buff_ay / buffersize;
  mean_az = buff_az / buffersize;
  mean_gx = buff_gx / buffersize;
  mean_gy = buff_gy / buffersize;
  mean_gz = buff_gz / buffersize;
}

void save() {
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.put(0, int(ax_offset));
  EEPROM.put(4, int(ay_offset));
  EEPROM.put(8, az_offset);
  EEPROM.put(12, gx_offset);
  EEPROM.put(16, gy_offset);
  EEPROM.put(20, gz_offset);
}


void calibration() {
  az_offset = -mean_az / 8;
  ay_offset = -mean_ay / 8;
  ax_offset = (16384 - mean_ax) / 8;
  gx_offset = -mean_gx / 4;
  gy_offset = -mean_gy / 4;
  gz_offset = -mean_gz / 4;

  while (1) {
    int ready = 0;

    mpu.setXAccelOffset(ax_offset);
    mpu.setYAccelOffset(ay_offset);
    mpu.setZAccelOffset(az_offset);
    mpu.setXGyroOffset(gx_offset);
    mpu.setYGyroOffset(gy_offset);
    mpu.setZGyroOffset(gz_offset);

    meansensors();

    if (abs(mean_az) <= acel_deadzone) ready++; else az_offset -= mean_az / acel_deadzone;
    if (abs(mean_ay) <= acel_deadzone) ready++; else ay_offset -= mean_ay / acel_deadzone;
    if (abs(16384 - mean_ax) <= acel_deadzone) ready++; else ax_offset += (16384 - mean_ax) / acel_deadzone;
    if (abs(mean_gx) <= giro_deadzone) ready++; else gx_offset -= mean_gx / (giro_deadzone + 1);
    if (abs(mean_gy) <= giro_deadzone) ready++; else gy_offset -= mean_gy / (giro_deadzone + 1);
    if (abs(mean_gz) <= giro_deadzone) ready++; else gz_offset -= mean_gz / (giro_deadzone + 1);

    if (ready == 6) break;
  }
}
