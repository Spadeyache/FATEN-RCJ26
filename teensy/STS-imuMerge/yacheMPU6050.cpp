#include "yacheMPU6050.h"
#include <EEPROM.h>

// EEPROM layout matches the original MPU6050-ALL03-yzplane sketch verbatim:
//   addr 0,4,8,12,16,20  →  ax,ay,az,gx,gy,gz  (4 bytes each, stored as int)
// Total 24 bytes; fits the IMU calib block reserved at 0x0000-0x001B in config.h.

// I2Cdev wireObj is void* — pass the chosen bus so all reads go through it.
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

    // ±2g accel, ±250°/s gyro (matches reference: raw / 16384 and raw / 131).
    _mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    _mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);
    _mpu.setDLPFMode(MPU6050_DLPF_BW_42);  // 42 Hz LPF — quiets vibration noise at 25 Hz sample rate

#if CALIBRATE_IMU
    calibrate();               // mpuCalib() — also calls saveOffsetsToEEPROM() at the end
#else
    loadOffsetsFromEEPROM();   // olddata() — read previously-saved offsets
#endif
    applyOffsets();

    _filter.begin(IMU_SAMPLE_HZ);
    _microsPerReading = (uint32_t)(1000000.0f / IMU_SAMPLE_HZ);
    _microsPrevious   = micros();

    Serial.printf("IMU+Madgwick ready @ %.1f Hz.\n", IMU_SAMPLE_HZ);

    // Let the filter converge on real gravity (pitch/roll) before snapping zero.
    Serial.println("IMU settling...");
    uint32_t settleStart = millis();
    while (millis() - settleStart < 2000) update();
    zeroAttitude();
    Serial.println("IMU ready.");
}

// One synchronous filter step at the next 25 Hz boundary.
void yacheMPU6050::sampleAndFilterOnce() {
    while ((int32_t)(micros() - _microsPrevious) < (int32_t)_microsPerReading) { /* spin */ }

    _mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);
    float ax = convertRawAccel(axRaw), ay = convertRawAccel(ayRaw), az = convertRawAccel(azRaw);
    float gx = convertRawGyro (gxRaw), gy = convertRawGyro (gyRaw), gz = convertRawGyro (gzRaw);
    _filter.updateIMU(gx, gy, gz, ax, ay, az);

    _roll  = _filter.getRoll();
    _pitch = _filter.getPitch();
    _yaw   = _filter.getYaw();

    _microsPrevious += _microsPerReading;
}

FASTRUN void yacheMPU6050::update() {
    if ((int32_t)(micros() - _microsPrevious) < (int32_t)_microsPerReading) return;

    _mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);
    float ax = convertRawAccel(axRaw), ay = convertRawAccel(ayRaw), az = convertRawAccel(azRaw);
    float gx = convertRawGyro (gxRaw), gy = convertRawGyro (gyRaw), gz = convertRawGyro (gzRaw);
    _filter.updateIMU(gx, gy, gz, ax, ay, az);

    _roll  = _filter.getRoll();
    _pitch = _filter.getPitch();
    _yaw   = _filter.getYaw();

    _microsPrevious += _microsPerReading;
}

float32_t yacheMPU6050::getYaw() {
    float y = _yaw - _yawZero;
    while (y >  180.0f) y -= 360.0f;
    while (y < -180.0f) y += 360.0f;
    return y;
}

void yacheMPU6050::zeroAttitude() {
    // Average a short burst of filter output as the zero reference.
    float sumY = 0, sumP = 0, sumR = 0;
    const int N = 25;  // ~1 s at 25 Hz
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

// Direct port of mpuCalib() from MPU6050-ALL03-yzplane.
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

// Direct port of calibration() from MPU6050-ALL03-yzplane (no max-iteration cap).
void yacheMPU6050::runAutoCalibration() {
    ax_offset = -mean_ax / 8;
    ay_offset = -mean_ay / 8;
    az_offset = (16384 - mean_az) / 8;
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

        if (abs(mean_ax)         <= acel_deadzone) ready++; else ax_offset -= mean_ax / acel_deadzone;
        if (abs(mean_ay)         <= acel_deadzone) ready++; else ay_offset -= mean_ay / acel_deadzone;
        if (abs(16384 - mean_az) <= acel_deadzone) ready++; else az_offset += (16384 - mean_az) / acel_deadzone;
        if (abs(mean_gx)         <= giro_deadzone) ready++; else gx_offset -= mean_gx / (giro_deadzone + 1);
        if (abs(mean_gy)         <= giro_deadzone) ready++; else gy_offset -= mean_gy / (giro_deadzone + 1);
        if (abs(mean_gz)         <= giro_deadzone) ready++; else gz_offset -= mean_gz / (giro_deadzone + 1);

        if (ready == 6) break;
    }
}

// Direct port of meansensors() from MPU6050-ALL03-yzplane.
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

// Direct port of olddata() from MPU6050-ALL03-yzplane.
// No magic check — trusts whatever's at addresses 0..23.
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

// Direct port of save() from MPU6050-ALL03-yzplane.
// NOTE: original wiped *all* EEPROM before writing. Here the wipe is scoped to
// the calib block (addresses 0..23) so it does NOT touch the map data that
// config.h reserves at EEPROM_MAP_BASE (0x0020).
void yacheMPU6050::saveOffsetsToEEPROM() {
    for (int i = 0; i < 24; i++) {
        EEPROM.write(i, 0);
    }
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
