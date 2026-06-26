#include "Mapping.h"
#include "../../config.h"
#include "../sensors/IMU.h"
#include "../sensors/ToF.h"
#include "../actions/Drive.h"
#include "../drivers/yacheVL53L7CX.h"

#include <Arduino.h>

// =============================================================================
//  Phase 0 — stripped to the pose estimator only.
//    Removed for now: EEPROM persistence (MapPersist), occupancy grid
//    (MapGrid / log-odds), and scan-match recalibration (Recalibrate).
//  These will be replaced incrementally by:
//    - refined 4x ToF processing
//    - a camera-FOV coverage grid (seen[])
//    - a landmark table (entrance / exit / rescue points)
// =============================================================================

namespace Processing {
namespace Mapping {

namespace {
    Pose     _pose;
    bool     _initialised     = false;
    float    _yaw_bias_rad    = 0.0f;
    uint32_t _last_predict_us = 0;

    float    _last_v_cmd      = 0.0f;
    float    _last_w_cmd      = 0.0f;

    inline float deg2rad(float d) { return d * (float)PI / 180.0f; }
    inline float yawToTheta(float yaw_deg) {
        return wrapAngle(deg2rad(yaw_deg) - _yaw_bias_rad);
    }

    // (v, omega) from commanded motor gains. Simple unicycle (no encoders).
    void readMotorCommand(float& v_mm_s, float& omega_rad_s) {
        const float left_avg  = 0.5f * (Actions::Drive::frontLeftGain()  + Actions::Drive::backLeftGain());
        const float right_avg = 0.5f * (Actions::Drive::frontRightGain() + Actions::Drive::backRightGain());

        const float scale_mmps = (1000.0f / FORWARD_MS_PER_MM) / (float)MAX_MOTOR_SPEED;
        const float vL = left_avg  * scale_mmps;
        const float vR = right_avg * scale_mmps;

        v_mm_s      = 0.5f * (vL + vR);
        omega_rad_s = (vR - vL) / (float)WHEELBASE_MM;
    }

    // Serial 't' helper: raw distance snapshot from every ToF (for bring-up).
    void dumpToF() {
        static int16_t mm[64];
        static uint8_t st[64];

        for (uint8_t s = 0; s < Sensors::ToF::count(); ++s) {
            yacheVL53L7CX& tof = Sensors::ToF::sensor(s);
            if (!tof.dataReady())        { Serial.printf("[tof %u] no data\n", s);  continue; }
            if (!tof.getRanges(mm, st))  { Serial.printf("[tof %u] read fail\n", s); continue; }

            const uint16_t n   = tof.zoneCount();
            const uint8_t  res = tof.resolution();
            const uint16_t ctr = (uint16_t)(res / 2) * res + (res / 2);   // ~center zone

            int16_t mn = 32767;
            for (uint16_t z = 0; z < n; ++z) {
                if (yacheVL53L7CX::isValid(st[z], mm[z]) && mm[z] < mn) mn = mm[z];
            }
            Serial.printf("[tof %u] center=%dmm st=%u  min=%dmm\n", s, mm[ctr], st[ctr], mn);
        }
    }
}

void init(bool /*restart*/) {
    poseInit(_pose, ENTRANCE_X_MM, ENTRANCE_Y_MM, 0.0f);
    _yaw_bias_rad    = deg2rad((float)Sensors::IMU::getYaw());

    Sensors::ToF::init();   // idempotent — assigns the 4 sensor addresses

    _last_predict_us = micros();
    _initialised     = true;

#if PRINT_MAPPING
    Serial.println(F("[mapping] initialised (phase0: pose-only)"));
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
}

void persist() {
    // Phase 0: EEPROM persistence disabled.
}

void handleSerial(char c) {
    if (!_initialised) return;
    switch (c) {
        case 'p':
            Serial.printf("[pose] x=%.1f y=%.1f th=%.2f rad\n",
                          _pose.x_mm, _pose.y_mm, _pose.theta);
            break;
        case 't':
            dumpToF();
            break;
        default: break;
    }
}

const Pose& pose() { return _pose; }

}  // namespace Mapping
}  // namespace Processing
