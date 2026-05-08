#include <SCServo.h>

SMS_STS sms_sts;
const int STS_EN = 2;

// Arrays to hold data for synchronous writing to multiple servos
byte ID[4] = {4, 1, 2, 3};      // Servo IDs
s16 Speed[4] = {0, 0, 0, 0};    // Target speeds 0-4000
byte ACC[4] = {150, 150, 150, 150};     // Acceleration values 0-254

void setup()
{
  // Initialize Serial1 at 1,000,000 baud (standard for STS servos)
  Serial1.begin(1000000);
  sms_sts.pSerial = &Serial1;
  pinMode(STS_EN, OUTPUT);
  delay(1000);

  // Set servos to Wheel Mode (Constant Speed Mode)
  sms_sts.WheelMode(4);
  sms_sts.WheelMode(1);
  sms_sts.WheelMode(2);
  sms_sts.WheelMode(3);
  digitalWrite(STS_EN, HIGH);
}

void loop()
{
  // Max Speed Calculation: 4000 * 0.732 ≈ 2928 RPM
  Speed[0] = 1400;
  Speed[1] = -1400;
  Speed[2] = 1400;
  Speed[3] = -1400;

  // Execute movement
  sms_sts.SyncWriteSpe(ID, 4, Speed, ACC);
  delay(1000);

  // Stop
  Speed[0] = 0;
  Speed[1] = 0;
  Speed[2] = 0;
  Speed[3] = 0;
  sms_sts.SyncWriteSpe(ID, 4, Speed, ACC);
  delay(2000);
}