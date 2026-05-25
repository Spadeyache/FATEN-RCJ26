#include "Mapping.h"
#include "config.h"
#include "MapGrid.h"
#include "MapPersist.h"
#include "Recalibrate.h"
#include "../sensors/IMU.h"
#include "../sensors/ToF.h"
#include "../actions/Drive.h"

#include <Arduino.h>
#include <Wire.h>

namespace Processing {
namespace Mapping {

namespace {
    Pose          _pose;
    EvacMap       _map;
    StuckDetector _stuck;

    bool          _initialised        = false;
    float         _yaw_bias_rad       = 0.0f;
    uint32_t      _last_predict_us    = 0;
    uint32_t      _last_checkpoint_ms = 0;

    int16_t       _tof_mm    [64];
    uint8_t       _tof_status[64];
    ScanRay       _last_rays [64 * TOF_COUNT];
    uint16_t      _last_rays_n = 0;

    float         _last_v_cmd = 0.0f;
    float         _last_w_cmd = 0.0f;

    inline float deg2rad(float d)         { return d * (float)PI / 180.0f; }
    inline float yawToTheta(float yaw_deg) {
        return wrapAngle(deg2rad(yaw_deg) - _yaw_bias_rad);
    }

    // (v, omega) from commanded motor gains. Phase 1: simple unicycle.
    void readMotorCommand(float& v_mm_s, float& omega_rad_s) {
        const float left_avg  = 0.5f * (Actions::Drive::frontLeftGain()  + Actions::Drive::backLeftGain());
        const float right_avg = 0.5f * (Actions::Drive::frontRightGain() + Actions::Drive::backRightGain());

        const float scale_mmps = (1000.0f / FORWARD_MS_PER_MM) / (float)MAX_MOTOR_SPEED;
        const float vL = left_avg  * scale_mmps;
        const float vR = right_avg * scale_mmps;

        v_mm_s      = 0.5f * (vL + vR);
        omega_rad_s = (vR - vL) / (float)WHEELBASE_MM;
    }
}

void init(bool restart) {
    bool loaded = false;
    if (restart) {
        loaded = mapPersistLoad(_pose, _map);
        if (loaded) {
            mapClearLogodds(_map);
            poseInflateCovariance(_pose, 50.0f, 0.1f);
#if PRINT_MAPPING
            Serial.println(F("[mapping] map restored from EEPROM"));
#endif
        } else {
#if PRINT_MAPPING
            Serial.println(F("[mapping] EEPROM invalid; fresh map"));
#endif
        }
    }
    if (!loaded) {
        mapPersistInit();
        mapInit(_map);
        poseInit(_pose, ENTRANCE_X_MM, ENTRANCE_Y_MM, 0.0f);
    }

    _yaw_bias_rad = deg2rad((float)Sensors::IMU::getYaw());

    Sensors::ToF::init();   // idempotent

    _stuck.reset();
    _last_predict_us    = micros();
    _last_checkpoint_ms = millis();
    _last_rays_n        = 0;
    _initialised        = true;

#if PRINT_MAPPING
    Serial.println(F("[mapping] initialised"));
#endif
}

static void tofPoll() {
    _last_rays_n = 0;

    for (uint8_t s = 0; s < Sensors::ToF::count(); ++s) {
        yacheVL53L7CX& tof = Sensors::ToF::sensor(s);
        if (!tof.dataReady()) continue;
        if (!tof.getRanges(_tof_mm, _tof_status)) continue;

        const uint16_t n    = tof.zoneCount();
        const float    mdx  = tof.mountDx();
        const float    mdy  = tof.mountDy();
        const float    myaw = tof.mountYaw();

        // Sensor origin in world frame (rotate mount offset by current θ):
        const float c  = cosf(_pose.theta);
        const float si = sinf(_pose.theta);
        const float rx = _pose.x_mm + (mdx * c - mdy * si);
        const float ry = _pose.y_mm + (mdx * si + mdy * c);
        const float sensor_yaw_world = wrapAngle(_pose.theta + myaw);

        for (uint16_t z = 0; z < n; ++z) {
            const int16_t mm = _tof_mm[z];
            const uint8_t st = _tof_status[z];
            if (mm < TOF_MIN_MM) continue;

            const float local_bearing = tof.zoneBearing(z);
            const float bearing       = wrapAngle(sensor_yaw_world + local_bearing);
            const bool  is_valid      = yacheVL53L7CX::isValid(st, mm);
            const float range         = is_valid ? (float)mm : (float)TOF_MAX_MM;

            mapIntegrateRay(_map, rx, ry, bearing, range, is_valid);

            if (is_valid && _last_rays_n < (uint16_t)(64 * TOF_COUNT)) {
                ScanRay& r = _last_rays[_last_rays_n++];
                r.bearing_local = local_bearing;
                r.range_mm      = (float)mm;
                r.mount_dx      = mdx;
                r.mount_dy      = mdy;
                r.mount_yaw     = myaw;
            }
        }
    }
}

static void maybeRecalibrate() {
    if (!_stuck.updateStuck(_pose, _last_v_cmd)) return;
    if (_last_rays_n < 4) return;

    Pose snapped;
    const int16_t margin = scanMatchPose(_map, _pose, _last_rays, _last_rays_n, snapped);
    constexpr int16_t RECAL_MARGIN_MIN = 4;
    if (margin >= RECAL_MARGIN_MIN) {
#if PRINT_MAPPING
        Serial.printf("[mapping] scan-match snap: dx=%.0f dy=%.0f dth=%.2f margin=%d\n",
                      snapped.x_mm - _pose.x_mm,
                      snapped.y_mm - _pose.y_mm,
                      wrapAngle(snapped.theta - _pose.theta),
                      (int)margin);
#endif
        _pose = snapped;
        poseInflateCovariance(_pose, 20.0f, 0.05f);
    }
}

static void maybeCheckpoint() {
    const uint32_t now = millis();
    if (now - _last_checkpoint_ms < (uint32_t)MAP_CHECKPOINT_MS) return;
    _last_checkpoint_ms = now;

    const uint16_t dirty = mapPersistDirtyCount(_map);
    if (dirty < (uint16_t)MAP_CHECKPOINT_MIN_DIRTY) return;

    const uint16_t writes = mapPersistSave(_pose, _map);
#if PRINT_MAPPING
    Serial.printf("[mapping] checkpoint: dirty=%u writes=%u\n", dirty, writes);
#else
    (void)writes;
#endif
}

void tick() {
    if (!_initialised) return;

    const uint32_t now_us = micros();
    const float    dt_s   = (now_us - _last_predict_us) * 1e-6f;
    _last_predict_us      = now_us;

    readMotorCommand(_last_v_cmd, _last_w_cmd);
    posePredict(_pose, _last_v_cmd, _last_w_cmd, dt_s);

    poseUpdateYaw(_pose, yawToTheta((float)Sensors::IMU::getYaw()));

    tofPoll();
    maybeRecalibrate();
    maybeCheckpoint();
}

void persist() {
    if (!_initialised) return;
    const uint16_t writes = mapPersistSave(_pose, _map);
#if PRINT_MAPPING
    Serial.printf("[mapping] persist: writes=%u\n", writes);
#else
    (void)writes;
#endif
}

void handleSerial(char c) {
    if (!_initialised) return;
    switch (c) {
        case 'm':
            mapDumpASCII(_map, _pose.x_mm, _pose.y_mm);
            break;
        case 'p':
            Serial.printf("[pose] x=%.1f y=%.1f th=%.2f rad  P=[%.2f %.2f %.4f]\n",
                          _pose.x_mm, _pose.y_mm, _pose.theta,
                          _pose.P[0], _pose.P[4], _pose.P[8]);
            break;
        default: break;
    }
}

const Pose& pose() { return _pose; }

}  // namespace Mapping
}  // namespace Processing
