#include "globals.h"
#include "yacheMPU6050.h"

// =============================================================================
//  Sensors.ino — MPU-6050 DMP init and update (Wire1, pins 17/16)
//  Exposes: initSensors(), updateSensors()
//
//  Yaw/pitch/roll are extracted from the DMP's onboard quaternion fusion.
//  The DMP runs at 100 Hz; updateSensors() is a no-op when no new FIFO packet
//  is available so it is safe to call every loop iteration.
// =============================================================================

yacheMPU6050 _imu(Wire1);  // DMP on Wire1 (SCL1=pin17, SDA1=pin16)

// --- Variable definitions (declared as extern in globals.h) ---
volatile float32_t pitch = 0.0f;
volatile float32_t roll  = 0.0f;
volatile float32_t yaw   = 0.0f;

volatile bool touchfront = false;
volatile bool conduct0 = false;
volatile bool conduct1 = false;

// ---------------------------------------------------------------------------

void initSensors() {
    // _imu.begin();          // Wire1.begin(), dmpInitialize, load EEPROM offsets
    // Serial.println("IMU ready.");
    
  pinMode(_touchfront, INPUT_PULLUP);
  pinMode(_conductPin0, INPUT_PULLUP);
  pinMode(_conductPin1, INPUT_PULLUP);
  Serial.println("Touch ready.");
}

// Called every loop iteration — returns immediately when no DMP packet is ready.
void updateSensors() {
    // _imu.update();
    // pitch = _imu.getPitch();
    // roll  = _imu.getRoll();
    // yaw   = _imu.getYaw();
    // Serial.print("pitch:"); Serial.print(pitch); 
    // Serial.print("roll:"); Serial.print(roll);
    // Serial.print("yaw:"); Serial.println(yaw);

    // HIGH(1) is OFF and LOW(0) is Touching 
    touchfront = !digitalRead(_touchfront);
    conduct0 = !digitalRead(_conductPin0);
    conduct1 = !digitalRead(_conductPin1);
}



