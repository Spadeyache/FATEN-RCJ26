#include "yacheMPU6050.h"

// Pass &w as the I2Cdev wireObj so all DMP transactions go through the chosen bus.
yacheMPU6050::yacheMPU6050(TwoWire &w)
    : _wire(&w), _mpu(MPU6050_DEFAULT_ADDRESS, (void*)&w) {}

void yacheMPU6050::begin() {
    _wire->begin();
    _wire->setClock(400000);

    _mpu.initialize();
    if (!_mpu.testConnection()) {
        Serial.println("MPU6050 connection failed! Check wiring.");
        while (1);
    }

    // DMP firmware upload (~1900 bytes over I2C — takes ~1 s at 400 kHz).
    uint8_t devStatus = _mpu.dmpInitialize();
    if (devStatus != 0) {
        Serial.printf("DMP init failed (code %u)\n", devStatus);
        while (1);
    }

    // NOTE: do NOT call setFullScaleGyroRange() here — dmpInitialize() already set ±2000°/s
    // and the DMP firmware is compiled for that range. Changing it would corrupt the DMP output.
    // Accel range is also left at dmpInitialize()'s ±2g default.
    _mpu.setDLPFMode(MPU6050_DLPF_BW_188);  // 188 Hz — ~1ms group delay
    _mpu.setRate(4);                          // explicit 200 Hz (1000/(1+4))

#if CALIBRATE_IMU
    calibrate();  // prints offsets to Serial; copy them into yacheMPU6050.h then set CALIBRATE_IMU=0
#else
    applyOffsets();
#endif

    _mpu.setDMPEnabled(true);
    _dmpReady = true;
    Serial.printf("IMU+DMP ready. FIFO packet: %u B\n", _mpu.dmpGetFIFOPacketSize());

    // Let the DMP's internal filter converge before zeroing.
    // The accelerometer correction needs ~1-2 s to pull the gyro integration to the true angle.
    Serial.println("IMU settling...");
    delay(2000);
    zeroAttitude();  // zero all axes at current resting orientation
    Serial.println("IMU ready.");
}

// Raw quaternion → yaw/pitch/roll in degrees (no EMA, no zero offset).
// NO axis remapping — standard DMP output (Z-up, sensor flat).
void yacheMPU6050::_computeYPR(float &yaw, float &pitch, float &roll) {
    Quaternion q;
    _mpu.dmpGetQuaternion(&q, _fifo);

    VectorFloat gravity;
    _mpu.dmpGetGravity(&gravity, &q);

    float ypr[3];
    _mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    yaw   = ypr[0] * (180.0f / (float)M_PI);
    pitch = ypr[1] * (180.0f / (float)M_PI);
    roll  = ypr[2] * (180.0f / (float)M_PI);
}

// Capture current orientation as the zero reference; resets EMA state.
// Called automatically by begin(); call again any time to re-zero.
void yacheMPU6050::zeroAttitude() {
    _mpu.resetFIFO();
    float sumY = 0, sumP = 0, sumR = 0;
    int n = 0;
    uint32_t start = millis();
    while (n < 50 && millis() - start < 2000) {
        if (_mpu.dmpGetCurrentFIFOPacket(_fifo)) {
            float y, p, r;
            _computeYPR(y, p, r);
            sumY += y;  sumP += p;  sumR += r;
            n++;
        }
    }
    if (n > 0) {
        _yawZero   = sumY / n;
        _pitchZero = sumP / n;
        _rollZero  = sumR / n;
    }
    _yaw = _pitch = _roll = 0.0f;  // reset EMA state to zero
}

FASTRUN void yacheMPU6050::update() {
    if (!_dmpReady) return;
    if (!_mpu.dmpGetCurrentFIFOPacket(_fifo)) return;

    float rawYaw, rawPitch, rawRoll;
    _computeYPR(rawYaw, rawPitch, rawRoll);

    // Apply zero offset
    float y = rawYaw   - _yawZero;
    float p = rawPitch - _pitchZero;
    float r = rawRoll  - _rollZero;

    // Wrap yaw to ±180°
    while (y >  180.0f) y -= 360.0f;
    while (y < -180.0f) y += 360.0f;

    // Exponential moving average — tune IMU_EMA_ALPHA in config.h
    _yaw   = IMU_EMA_ALPHA * y + (1.0f - IMU_EMA_ALPHA) * _yaw;
    _pitch = IMU_EMA_ALPHA * p + (1.0f - IMU_EMA_ALPHA) * _pitch;
    _roll  = IMU_EMA_ALPHA * r + (1.0f - IMU_EMA_ALPHA) * _roll;
}

void yacheMPU6050::applyOffsets() {
    _mpu.setXAccelOffset(ax_offset);
    _mpu.setYAccelOffset(ay_offset);
    _mpu.setZAccelOffset(az_offset);
    _mpu.setXGyroOffset(gx_offset);
    _mpu.setYGyroOffset(gy_offset);
    _mpu.setZGyroOffset(gz_offset);
}

void yacheMPU6050::calibrate() {
    Serial.println("=== IMU CALIBRATION START — keep robot flat and still ===");
    ax_offset = ay_offset = az_offset = 0;
    gx_offset = gy_offset = gz_offset = 0;
    applyOffsets();
    delay(1000);

    meansensors();
    Serial.printf("Raw means  ax=%d ay=%d az=%d | gx=%d gy=%d gz=%d\n",
                  mean_ax, mean_ay, mean_az, mean_gx, mean_gy, mean_gz);

    runAutoCalibration();
    meansensors();

    Serial.println("=== PASTE THESE INTO yacheMPU6050.h, then set CALIBRATE_IMU 0 ===");
    Serial.printf("    int16_t ax_offset = %d, ay_offset = %d, az_offset = %d;\n",
                  ax_offset, ay_offset, az_offset);
    Serial.printf("    int16_t gx_offset = %d, gy_offset = %d, gz_offset = %d;\n",
                  gx_offset, gy_offset, gz_offset);
    Serial.println("=== Final check means (ax should be ~16384; ay, az, gyros should be ~0) ===");
    Serial.printf("    ax=%d ay=%d az=%d | gx=%d gy=%d gz=%d\n",
                  mean_ax, mean_ay, mean_az, mean_gx, mean_gy, mean_gz);
}

void yacheMPU6050::runAutoCalibration() {
    az_offset = (int16_t)(-mean_az / 8);
    ay_offset = (int16_t)(-mean_ay / 8);
    ax_offset = (int16_t)((16384 - mean_ax) / 8);
    gx_offset = (int16_t)(-mean_gx / 4);
    gy_offset = (int16_t)(-mean_gy / 4);
    gz_offset = (int16_t)(-mean_gz / 4);

    for (uint16_t cycles = 0; cycles < 100; ++cycles) {
        int8_t ready = 0;
        applyOffsets();
        meansensors();
        if (abs(mean_az)           <= acel_deadzone) ready++; else az_offset -= mean_az / acel_deadzone;
        if (abs(mean_ay)           <= acel_deadzone) ready++; else ay_offset -= mean_ay / acel_deadzone;
        if (abs(16384 - mean_ax)   <= acel_deadzone) ready++; else ax_offset += (16384 - mean_ax) / acel_deadzone;
        if (abs(mean_gx)           <= giro_deadzone) ready++; else gx_offset -= mean_gx / (giro_deadzone + 1);
        if (abs(mean_gy)           <= giro_deadzone) ready++; else gy_offset -= mean_gy / (giro_deadzone + 1);
        if (abs(mean_gz)           <= giro_deadzone) ready++; else gz_offset -= mean_gz / (giro_deadzone + 1);
        if (ready == 6) break;
        delay(2);
    }
}

void yacheMPU6050::meansensors() {
    int64_t buff_ax=0, buff_ay=0, buff_az=0;
    int64_t buff_gx=0, buff_gy=0, buff_gz=0;
    for (int32_t i = 0; i < buffersize + 100; ++i) {
        _mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);
        if (i > 100) {
            buff_ax += axRaw; buff_ay += ayRaw; buff_az += azRaw;
            buff_gx += gxRaw; buff_gy += gyRaw; buff_gz += gzRaw;
        }
        delay(2);
    }
    mean_ax = (int32_t)(buff_ax / buffersize);
    mean_ay = (int32_t)(buff_ay / buffersize);
    mean_az = (int32_t)(buff_az / buffersize);
    mean_gx = (int32_t)(buff_gx / buffersize);
    mean_gy = (int32_t)(buff_gy / buffersize);
    mean_gz = (int32_t)(buff_gz / buffersize);
}


FLASHMEM void yacheMPU6050::printQuat() {
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 250) {
        Serial.printf("Yaw:%7.2f  Pit:%7.2f  Rol:%7.2f\n", _yaw, _pitch, _roll);
        lastPrint = millis();
    }
}
