#include "globals.h"
#include "yacheMPU6050.h"

// =============================================================================
//  Sensors.ino — MPU-6050 + Madgwick init and update (Wire1, pins 17/16)
//  Exposes: initSensors(), updateSensors()
//
//  Yaw/pitch/roll are produced by the Madgwick filter at IMU_SAMPLE_HZ.
//  yacheMPU6050::update() is internally throttled so it is safe to call every
//  loop iteration — it returns immediately when no new sample window is due.
// =============================================================================

yacheMPU6050 _imu(Wire1);  // MPU6050 on Wire1 (SCL1=pin17, SDA1=pin16)

// --- Variable definitions (declared as extern in globals.h) ---
volatile float32_t pitch = 0.0f;
volatile float32_t roll  = 0.0f;
volatile float32_t yaw   = 0.0f;

volatile bool touchfront = false;
volatile bool conduct0 = false;
volatile bool conduct1 = false;

// ---------------------------------------------------------------------------

void initSensors() {
    _imu.begin();          // Wire1.begin(), MPU init, optional calibrate, Madgwick, settle, zeroAttitude
    Serial.println("IMU ready.");

  pinMode(_touchfront, INPUT_PULLUP);
  pinMode(_conductPin0, INPUT_PULLUP);
  pinMode(_conductPin1, INPUT_PULLUP);
  Serial.println("Touch ready.");
}

// Called every loop iteration — internally throttled to IMU_SAMPLE_HZ.
void updateSensors() {
    _imu.update();
    pitch = _imu.getPitch();
    roll  = _imu.getRoll();
    yaw   = _imu.getYaw();

    // Throttle debug print to 10 Hz — the previous unthrottled spam at
    // ~kHz loop rate saturated the serial port and made the values unreadable.
    static uint32_t lastImuPrint = 0;
    if (millis() - lastImuPrint >= 100) {
        Serial.printf("pitch:%.2f roll:%.2f yaw:%.2f\n",
                      (float)pitch, (float)roll, (float)yaw);
        lastImuPrint = millis();
    }

    // HIGH(1) is OFF and LOW(0) is Touching
    touchfront = !digitalRead(_touchfront);
    conduct0 = digitalRead(_conductPin0);
    conduct1 = !digitalRead(_conductPin1);
}



