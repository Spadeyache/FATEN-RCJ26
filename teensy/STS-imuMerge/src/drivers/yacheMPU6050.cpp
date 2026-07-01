#include "yacheMPU6050.h"
#include <EEPROM.h>

// EEPROM layout: addrs 0,4,8,12,16,20 → ax,ay,az,gx,gy,gz (4 bytes each).
// Fits the calib block reserved at 0x0000..0x001B in config.h.

yacheMPU6050::yacheMPU6050(TwoWire &w)
    : _wire(&w), _mpu(MPU6050_DEFAULT_ADDRESS, (void*)&w) {}

void yacheMPU6050::begin() {
    _wire->begin();
    _wire->setClock(400000);   // 400 kHz fast-mode: trims the blocking getMotion6()
                               // read that runs inside the timer ISR. Drop back to
                               // default if the IMU bus ever NAKs / testConnection fails.

    // Bare initialize() only — matches IMU-01 (defaults: ±2g, ±250°/s, DLPF off).
    _mpu.initialize();
    if (!_mpu.testConnection()) {
        Serial.println("MPU6050 connection failed! Check wiring.");
        while (1);
    }

#if CALIBRATE_IMU
    calibrate();              // measures offsets, applies them, saves to EEPROM
#else
    loadOffsetsFromEEPROM();  // reuse the last calibration stored in EEPROM
#endif
    applyOffsets();

    _filter.begin(IMU_SAMPLE_HZ);
    _microsPerReading = (uint32_t)(1000000.0f / IMU_SAMPLE_HZ);
    _microsPrevious   = micros();

    // No settle / zeroAttitude — output is absolute, exactly like IMU-01.
    Serial.printf("IMU+Madgwick ready @ %.1f Hz.\n", IMU_SAMPLE_HZ);
}

FASTRUN void yacheMPU6050::gateAccelForSpin(float &ax, float &ay, float &az) {
    const float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (fabsf(norm - 1.0f) > ACCEL_GATE_DEVIATION_G) {
        ax = _lastGoodAx; ay = _lastGoodAy; az = _lastGoodAz;
        return;
    }
    _lastGoodAx = ax; _lastGoodAy = ay; _lastGoodAz = az;
}

void yacheMPU6050::sampleAndFilterOnce() {
    while ((int32_t)(micros() - _microsPrevious) < (int32_t)_microsPerReading) { /* spin */ }

    _mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);
    float ax = convertRawAccel(axRaw), ay = convertRawAccel(ayRaw), az = convertRawAccel(azRaw);
    float gx = convertRawGyro (gxRaw), gy = convertRawGyro (gyRaw), gz = convertRawGyro (gzRaw);
    gateAccelForSpin(ax, ay, az);
    // Axis remap + 180° flip about sensor-Y (this mount = IMU-01 rotated 180°, X
    // reversed). Negate the X and Z feeds on BOTH gyro and accel: two sign flips
    // keep the frame right-handed (negating X alone would NOT), and this puts
    // gravity back on filter +Z, so the rest attitude is ~0 — no 0→180 sweep.
    _filter.updateIMU(-gy, gz, -gx, -ay, az, -ax);
    // _filter.updateIMU(gy, gz, gx, ay, az, ax);

    _roll  = _filter.getRoll();
    _pitch = _filter.getPitch();
    _yaw   = _filter.getYaw();

    _microsPrevious += _microsPerReading;
}

FASTRUN void yacheMPU6050::update() {
    // Driven by the IMU IntervalTimer ISR at a fixed IMU_SAMPLE_HZ. The timer IS
    // the sample clock, so there is no micros() re-gate and no per-sample dt: the
    // fixed interval already matches the invSampleFreq begin() handed Madgwick.
    // (Dropping the old measured-dt path removes the sample-drop / dt-doubling
    // jitter that a slightly-early ISR entry used to inject into pitch/roll.)
    _mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);
    float ax = convertRawAccel(axRaw), ay = convertRawAccel(ayRaw), az = convertRawAccel(azRaw);
    float gx = convertRawGyro (gxRaw), gy = convertRawGyro (gyRaw), gz = convertRawGyro (gzRaw);
    gateAccelForSpin(ax, ay, az);

#if PRINT_IMU
    // TEMP mount-check: at rest, the axis reading ~±16384 is the gravity axis.
    static uint32_t _lpRaw = 0;
    if (millis() - _lpRaw >= 200) {
        Serial.printf("RAW a[%6d %6d %6d] g[%6d %6d %6d]\n",
                      axRaw, ayRaw, azRaw, gxRaw, gyRaw, gzRaw);
        _lpRaw = millis();
    }
#endif

    // Axis remap + 180° flip about sensor-Y (this mount = IMU-01 rotated 180°, X
    // reversed). Negate the X and Z feeds on BOTH gyro and accel: two sign flips
    // keep the frame right-handed (negating X alone would NOT), and this puts
    // gravity back on filter +Z, so the rest attitude is ~0 — no 0→180 sweep.
    // _filter.updateIMU(gy, gz, gx, ay, az, ax);
    _filter.updateIMU(-gy, gz, -gx, -ay, az, -ax);
    

    _roll  = _filter.getRoll();
    _pitch = _filter.getPitch();
    _yaw   = _filter.getYaw();
}

float32_t yacheMPU6050::getYaw() {
    float y = _yaw - _yawZero;
    while (y >  180.0f) y -= 360.0f;
    while (y < -180.0f) y += 360.0f;
    return y;
}

void yacheMPU6050::zeroAttitude() {
    float sumY = 0, sumP = 0, sumR = 0;
    const int N = 25;
    for (int i = 0; i < N; ++i) {
        sampleAndFilterOnce();
        sumY += _yaw;  sumP += _pitch;  sumR += _roll;
    }
    _yawZero   = sumY / N;
    _pitchZero = sumP / N;
    _rollZero  = sumR / N;
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
    Serial.println("Starting calibration...");

    _mpu.setXAccelOffset(0);
    _mpu.setYAccelOffset(0);
    _mpu.setZAccelOffset(0);
    _mpu.setXGyroOffset(0);
    _mpu.setYGyroOffset(0);
    _mpu.setZGyroOffset(0);
    delay(1000);

    Serial.println("Calibration In Prog...");
    meansensors();
    Serial.println("Calibration In Prog...");
    runAutoCalibration();
    Serial.println("Calibration In Prog...");
    meansensors();

    Serial.println("\nFINISHED!");
    Serial.println("Use these defines in your code:");
    Serial.print("int ax_offset = "); Serial.print(ax_offset); Serial.println(";");
    Serial.print("int ay_offset = "); Serial.print(ay_offset); Serial.println(";");
    Serial.print("int az_offset = "); Serial.print(az_offset); Serial.println(";");
    Serial.print("int gx_offset = "); Serial.print(gx_offset); Serial.println(";");
    Serial.print("int gy_offset = "); Serial.print(gy_offset); Serial.println(";");
    Serial.print("int gz_offset = "); Serial.print(gz_offset); Serial.println(";");

    Serial.println("Saving Data...");
    saveOffsetsToEEPROM();
    delay(600);
}

void yacheMPU6050::runAutoCalibration() {
    // Gravity rests on X, but the mount may have it on +X or -X. Auto-detect the
    // sign from the first reading so the offsets converge either way (handles a
    // 180°-flipped sensor without code changes).
    const int gTarget = (mean_ax >= 0) ? 16384 : -16384;

    ax_offset = (gTarget - mean_ax) / 8;
    ay_offset = -mean_ay / 8;
    az_offset = -mean_az / 8;
    gx_offset = -mean_gx / 4;
    gy_offset = -mean_gy / 4;
    gz_offset = -mean_gz / 4;

    while (1) {
        int ready = 0;

        _mpu.setXAccelOffset(ax_offset);
        _mpu.setYAccelOffset(ay_offset);
        _mpu.setZAccelOffset(az_offset);
        _mpu.setXGyroOffset(gx_offset);
        _mpu.setYGyroOffset(gy_offset);
        _mpu.setZGyroOffset(gz_offset);

        meansensors();

        if (abs(gTarget - mean_ax) <= acel_deadzone) ready++; else ax_offset += (gTarget - mean_ax) / acel_deadzone;
        if (abs(mean_ay)           <= acel_deadzone) ready++; else ay_offset -= mean_ay / acel_deadzone;
        if (abs(mean_az)           <= acel_deadzone) ready++; else az_offset -= mean_az / acel_deadzone;
        if (abs(mean_gx)           <= giro_deadzone) ready++; else gx_offset -= mean_gx / (giro_deadzone + 1);
        if (abs(mean_gy)           <= giro_deadzone) ready++; else gy_offset -= mean_gy / (giro_deadzone + 1);
        if (abs(mean_gz)           <= giro_deadzone) ready++; else gz_offset -= mean_gz / (giro_deadzone + 1);

        if (ready == 6) break;
    }
}

void yacheMPU6050::meansensors() {
    long i = 0, buff_ax = 0, buff_ay = 0, buff_az = 0;
    long buff_gx = 0, buff_gy = 0, buff_gz = 0;

    for (i = 0; i < (buffersize + 100); i++) {
        _mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);
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

bool yacheMPU6050::loadOffsetsFromEEPROM() {
    int v;
    EEPROM.get(0,  v); ax_offset = (int16_t)v;
    EEPROM.get(4,  v); ay_offset = (int16_t)v;
    EEPROM.get(8,  v); az_offset = (int16_t)v;
    EEPROM.get(12, v); gx_offset = (int16_t)v;
    EEPROM.get(16, v); gy_offset = (int16_t)v;
    EEPROM.get(20, v); gz_offset = (int16_t)v;

    Serial.print("int ax_offset = "); Serial.print(ax_offset); Serial.println(";");
    Serial.print("int ay_offset = "); Serial.print(ay_offset); Serial.println(";");
    Serial.print("int az_offset = "); Serial.print(az_offset); Serial.println(";");
    Serial.print("int gx_offset = "); Serial.print(gx_offset); Serial.println(";");
    Serial.print("int gy_offset = "); Serial.print(gy_offset); Serial.println(";");
    Serial.print("int gz_offset = "); Serial.print(gz_offset); Serial.println(";");
    return true;
}

void yacheMPU6050::saveOffsetsToEEPROM() {
    // Scoped wipe — only the IMU's reserved bytes; doesn't touch map data.
    for (int i = 0; i < 24; i++) EEPROM.write(i, 0);
    EEPROM.put(0,  (int)ax_offset);
    EEPROM.put(4,  (int)ay_offset);
    EEPROM.put(8,  (int)az_offset);
    EEPROM.put(12, (int)gx_offset);
    EEPROM.put(16, (int)gy_offset);
    EEPROM.put(20, (int)gz_offset);
}

FLASHMEM void yacheMPU6050::printQuat() {
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 250) {
        Serial.printf("Yaw:%7.2f  Pit:%7.2f  Rol:%7.2f\n", _yaw, _pitch, _roll);
        lastPrint = millis();
    }
}
