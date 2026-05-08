#include "globals.h"
#include "Pose.h"
#include "MapGrid.h"
#include "Recalibrate.h"
#include "MapPersist.h"
#include "yacheVL53L7CX.h"

// =============================================================================
//  Mapping.ino — Evacuation-zone mapping orchestrator
//
//  Pipeline (called from handleEvacuationZone() each loop):
//    mappingTick():
//      1. tofPoll()          — non-blocking ToF frame check + integration
//      2. posePredict()      — motor command + dt
//      3. poseUpdateYaw()    — IMU yaw measurement
//      4. mapIntegrateRay() per valid ToF zone
//      5. (future) maybeRecalibrate(), maybeCheckpoint()
//
//  Yaw bias is captured on EVAC entry so theta=0 corresponds to the entry
//  heading regardless of MPU absolute reference.
// =============================================================================

// --- Variable definitions (declared extern in globals.h) ---
Pose          g_pose;
EvacMap       g_map;
yacheVL53L7CX g_tof[TOF_COUNT] = {
    yacheVL53L7CX(Wire1, TOF_LPN_PIN_FRONT, TOF_I2C_RST_PIN),
};

// --- Module-private state ---
namespace {
    bool          _initialised = false;
    float         _yaw_bias_rad = 0.0f;     // Madgwick yaw at entry; subtracted to get robot-frame theta
    uint32_t      _last_predict_us = 0;
    uint32_t      _last_checkpoint_ms = 0;
    StuckDetector _stuck;
    int16_t       _tof_mm    [64];          // scratch for ToF frame (max 8x8 = 64)
    uint8_t       _tof_status[64];

    // Buffer of valid endpoint rays from the most recent ToF poll, used by scan-match.
    ScanRay       _last_rays [64 * TOF_COUNT];
    uint16_t      _last_rays_n = 0;
    float         _last_v_cmd  = 0.0f;
    float         _last_w_cmd  = 0.0f;

    inline float deg2rad(float d) { return d * (float)PI / 180.0f; }

    // Convert Madgwick yaw (degrees, absolute) into robot-frame theta (radians, [-π, π]).
    inline float yawToTheta(float yaw_deg) {
        return wrapAngle(deg2rad(yaw_deg) - _yaw_bias_rad);
    }

    // Estimate (v, omega) from current motor gains. Phase 1: simple unicycle
    // model averaging the two sides.
    void readMotorCommand(float& v_mm_s, float& omega_rad_s) {
        // Drive.ino flips signs for right motors before sending to STS, but the
        // GAIN globals here are the *as-commanded* values. Convention:
        //   forward → all gains positive; spin CCW → left negative, right positive.
        const float left_avg  = 0.5f * ((float)frontLeftGain  + (float)backLeftGain);
        const float right_avg = 0.5f * ((float)frontRightGain + (float)backRightGain);

        // mm/s per unit gain at MAX_MOTOR_SPEED → 1000/FORWARD_MS_PER_MM mm/s.
        const float scale_mmps = (1000.0f / FORWARD_MS_PER_MM) / (float)MAX_MOTOR_SPEED;
        const float vL = left_avg  * scale_mmps;
        const float vR = right_avg * scale_mmps;

        v_mm_s      = 0.5f * (vL + vR);
        omega_rad_s = (vR - vL) / (float)WHEELBASE_MM;
    }
}

// ---------------------------------------------------------------------------

void mappingInit(bool restart) {
    // Map: try to restore from EEPROM if requested, else start fresh.
    bool loaded_from_eeprom = false;
    if (restart) {
        loaded_from_eeprom = mapPersistLoad(g_pose, g_map);
        if (loaded_from_eeprom) {
            mapClearLogodds(g_map);
            // Pose came from EEPROM; inflate covariance because re-entry has uncertainty.
            poseInflateCovariance(g_pose, 50.0f, 0.1f);
            Serial.println(F("[mapping] map restored from EEPROM"));
        } else {
            Serial.println(F("[mapping] EEPROM invalid; fresh map"));
        }
    }
    if (!loaded_from_eeprom) {
        mapPersistInit();   // populates shadow with UNKNOWN so first save is full
        mapInit(g_map);
        poseInit(g_pose, ENTRANCE_X_MM, ENTRANCE_Y_MM, 0.0f);
    }

    // Yaw bias snapshot — current Madgwick yaw becomes our θ=0 reference.
    _yaw_bias_rad = deg2rad((float)yaw);

    // ToF bring-up. Wire1 begin() is idempotent; call here so order doesn't
    // matter relative to Sensors.ino (which uses Wire).
    Wire1.begin();
    g_tof[0].setMountTransform(TOF_FRONT_DX_MM, TOF_FRONT_DY_MM, TOF_FRONT_YAW_RAD);
    if (!g_tof[0].begin()) {
        Serial.println(F("[mapping] ToF[0] init FAILED"));
    } else {
        Serial.println(F("[mapping] ToF[0] ready"));
    }

    _stuck.reset();
    _last_predict_us    = micros();
    _last_checkpoint_ms = millis();
    _last_rays_n        = 0;
    _initialised        = true;
    Serial.println(F("[mapping] initialised"));
}

// ---------------------------------------------------------------------------

static void tofPoll() {
    _last_rays_n = 0;

    for (uint8_t s = 0; s < TOF_COUNT; ++s) {
        if (!g_tof[s].dataReady()) continue;
        if (!g_tof[s].getRanges(_tof_mm, _tof_status)) continue;

        const uint16_t n    = g_tof[s].zoneCount();
        const float    mdx  = g_tof[s].mountDx();
        const float    mdy  = g_tof[s].mountDy();
        const float    myaw = g_tof[s].mountYaw();

        // Sensor origin in world frame (rotate mount offset by current θ):
        const float c  = cosf(g_pose.theta);
        const float si = sinf(g_pose.theta);
        const float rx = g_pose.x_mm + (mdx * c - mdy * si);
        const float ry = g_pose.y_mm + (mdx * si + mdy * c);
        const float sensor_yaw_world = wrapAngle(g_pose.theta + myaw);

        for (uint16_t z = 0; z < n; ++z) {
            const int16_t mm = _tof_mm[z];
            const uint8_t st = _tof_status[z];

            if (mm < TOF_MIN_MM) continue;

            const float local_bearing = g_tof[s].zoneBearing(z);
            const float bearing       = wrapAngle(sensor_yaw_world + local_bearing);
            const bool  is_valid      = yacheVL53L7CX::isValid(st, mm);
            const float range         = is_valid ? (float)mm : (float)TOF_MAX_MM;

            mapIntegrateRay(g_map, rx, ry, bearing, range, is_valid);

            // Stash valid endpoints for scan-match.
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

// ---------------------------------------------------------------------------
//  Recalibration: triggered when motion stalls despite a forward command.
//  Runs scan-match around current pose; snaps if margin exceeds threshold.
// ---------------------------------------------------------------------------
static void maybeRecalibrate() {
    if (!_stuck.updateStuck(g_pose, _last_v_cmd)) return;
    if (_last_rays_n < 4) return;   // not enough ToF data to score reliably

    Pose snapped;
    const int16_t margin = scanMatchPose(g_map, g_pose, _last_rays, _last_rays_n, snapped);
    constexpr int16_t RECAL_MARGIN_MIN = 4;   // candidate must beat the seed by ≥4 net rays
    if (margin >= RECAL_MARGIN_MIN) {
        Serial.printf("[mapping] scan-match snap: dx=%.0f dy=%.0f dth=%.2f margin=%d\n",
                      snapped.x_mm - g_pose.x_mm,
                      snapped.y_mm - g_pose.y_mm,
                      wrapAngle(snapped.theta - g_pose.theta),
                      (int)margin);
        g_pose = snapped;
        // Inflate covariance to reflect that this was a discrete jump, not a smooth update.
        poseInflateCovariance(g_pose, 20.0f, 0.05f);
    }
}

// ---------------------------------------------------------------------------
//  Watchdog checkpoint: every MAP_CHECKPOINT_MS, save if dirty enough.
// ---------------------------------------------------------------------------
static void maybeCheckpoint() {
    const uint32_t now = millis();
    if (now - _last_checkpoint_ms < (uint32_t)MAP_CHECKPOINT_MS) return;
    _last_checkpoint_ms = now;

    const uint16_t dirty = mapPersistDirtyCount(g_map);
    if (dirty < (uint16_t)MAP_CHECKPOINT_MIN_DIRTY) return;

    const uint16_t writes = mapPersistSave(g_pose, g_map);
    Serial.printf("[mapping] checkpoint: dirty=%u writes=%u\n", dirty, writes);
}

// ---------------------------------------------------------------------------
//  K230 evacuation-point snap (PHASE 2) — disabled in phase 1.
// ---------------------------------------------------------------------------
// Once the four zone corners have been discovered (via ToF wall-fit) and
// stored in `corners[4]`, each K230 EVAC_RED / EVAC_GRN detection's
// normalised x in [0..255] tells us the rough bearing relative to robot
// heading. Pick the nearest of the four known corners and stamp a 3x3 cell
// patch with CS_EVAC_RED / CS_EVAC_GRN.
//
// for (uint8_t i = 0; i < detectionCount; ++i) {
//     if (detections[i].type != K230_EVAC_RED &&
//         detections[i].type != K230_EVAC_GREEN) continue;
//
//     const float cam_bearing = ((float)detections[i].x - 127.5f) / 127.5f
//                               * (CAMERA_FOV_DEG * 0.5f) * (PI / 180.0f);
//     const float world_bearing = wrapAngle(g_pose.theta + cam_bearing);
//     const Corner& c = nearestCornerByBearing(world_bearing, corners);
//
//     const CellSemantic s = (detections[i].type == K230_EVAC_RED)
//                                ? CS_EVAC_RED : CS_EVAC_GRN;
//     stampCornerPatch(g_map, c.cx, c.cy, /*radius=*/2, s);
// }
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

void mappingTick() {
    if (!_initialised) return;

    // 1. Predict using motor command since previous tick.
    const uint32_t now_us = micros();
    const float    dt_s   = (now_us - _last_predict_us) * 1e-6f;
    _last_predict_us      = now_us;

    readMotorCommand(_last_v_cmd, _last_w_cmd);
    posePredict(g_pose, _last_v_cmd, _last_w_cmd, dt_s);

    // 2. IMU yaw fusion.
    poseUpdateYaw(g_pose, yawToTheta((float)yaw));

    // 3. ToF poll → log-odds + ray buffer for scan-match.
    tofPoll();

    // 4. Recalibration + checkpoint.
    maybeRecalibrate();
    maybeCheckpoint();
}

void mappingPersist() {
    if (!_initialised) return;
    const uint16_t writes = mapPersistSave(g_pose, g_map);
    Serial.printf("[mapping] persist: writes=%u\n", writes);
}

// ---------------------------------------------------------------------------
//  Debug command handler — call from main loop or wherever Serial chars are
//  consumed. 'm' dumps the map ASCII. 'p' prints pose.
// ---------------------------------------------------------------------------
void mappingHandleSerial(char c) {
    if (!_initialised) return;
    switch (c) {
        case 'm':
            mapDumpASCII(g_map, g_pose.x_mm, g_pose.y_mm);
            break;
        case 'p':
            Serial.printf("[pose] x=%.1f y=%.1f th=%.2f rad  P=[%.2f %.2f %.4f]\n",
                          g_pose.x_mm, g_pose.y_mm, g_pose.theta,
                          g_pose.P[0], g_pose.P[4], g_pose.P[8]);
            break;
        default: break;
    }
}
