#include "Recalibrate.h"
#include <math.h>

// =============================================================================
//  Recalibrate implementation
// =============================================================================

void StuckDetector::reset() {
    _armed    = false;
    _arm_ms   = 0;
    _arm_x_mm = 0.0f;
    _arm_y_mm = 0.0f;
}

bool StuckDetector::updateStuck(const Pose& p, float v_cmd_mm_s) {
    const uint32_t now = millis();
    const bool driving = fabsf(v_cmd_mm_s) >= (float)RECAL_MIN_VCMD_MMPS;

    if (!driving) { _armed = false; return false; }

    if (!_armed) {
        _armed    = true;
        _arm_ms   = now;
        _arm_x_mm = p.x_mm;
        _arm_y_mm = p.y_mm;
        return false;
    }

    const float dx = p.x_mm - _arm_x_mm;
    const float dy = p.y_mm - _arm_y_mm;
    const float d  = sqrtf(dx * dx + dy * dy);

    if (d > (float)RECAL_DPOSE_MM) {
        // We did move — re-arm baseline and try again.
        _arm_ms   = now;
        _arm_x_mm = p.x_mm;
        _arm_y_mm = p.y_mm;
        return false;
    }

    if (now - _arm_ms >= (uint32_t)RECAL_STUCK_MS) {
        _armed = false;       // one-shot; require re-arming after action
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  scoreScan — for each ray endpoint at the candidate pose, look up the cell
//  the map currently believes is there. Reward agreement with WALL.
// ---------------------------------------------------------------------------
int16_t scoreScan(const EvacMap& m,
                  const Pose&    cand,
                  const ScanRay* rays,
                  uint16_t       n_rays)
{
    if (!rays || n_rays == 0) return 0;

    const float c   = cosf(cand.theta);
    const float si  = sinf(cand.theta);

    int16_t score = 0;
    for (uint16_t i = 0; i < n_rays; ++i) {
        const ScanRay& r = rays[i];

        // Sensor origin in world frame (under candidate pose).
        const float rx = cand.x_mm + (r.mount_dx * c - r.mount_dy * si);
        const float ry = cand.y_mm + (r.mount_dx * si + r.mount_dy * c);
        const float bw = wrapAngle(cand.theta + r.mount_yaw + r.bearing_local);

        const float ex = rx + r.range_mm * cosf(bw);
        const float ey = ry + r.range_mm * sinf(bw);

        const int16_t cx = worldToCellX(ex);
        const int16_t cy = worldToCellY(ey);
        if (!inBounds(cx, cy)) { score -= 1; continue; }

        const CellSemantic s = (CellSemantic)m.semantic[idx((uint8_t)cx, (uint8_t)cy)];
        switch (s) {
            case CS_WALL:        score += 1; break;
            case CS_OBSTACLE:    score += 2; break;
            case CS_EVAC_RED:
            case CS_EVAC_GRN:    score += 2; break;
            case CS_FREE:        score -= 1; break;
            case CS_UNKNOWN:
            default:             /* no contribution */ break;
        }
    }
    return score;
}

// ---------------------------------------------------------------------------
//  scanMatchPose — 9 x 9 x 11 grid search by default (parameters in config.h).
// ---------------------------------------------------------------------------
int16_t scanMatchPose(const EvacMap& m,
                      const Pose&    seed,
                      const ScanRay* rays,
                      uint16_t       n_rays,
                      Pose&          out)
{
    const float deg2rad_f = (float)PI / 180.0f;
    const int   nxy_steps = (int)(RECAL_WIN_XY_MM / RECAL_STEP_XY_MM); // each side
    const int   nth_steps = (int)(RECAL_WIN_TH_DEG / RECAL_STEP_TH_DEG);

    const int16_t center_score = scoreScan(m, seed, rays, n_rays);
    int16_t best_score = center_score;
    Pose    best       = seed;

    Pose cand = seed;
    for (int ix = -nxy_steps; ix <= nxy_steps; ++ix) {
        cand.x_mm = seed.x_mm + ix * (float)RECAL_STEP_XY_MM;
        for (int iy = -nxy_steps; iy <= nxy_steps; ++iy) {
            cand.y_mm = seed.y_mm + iy * (float)RECAL_STEP_XY_MM;
            for (int it = -nth_steps; it <= nth_steps; ++it) {
                cand.theta = wrapAngle(seed.theta + it * RECAL_STEP_TH_DEG * deg2rad_f);
                const int16_t s = scoreScan(m, cand, rays, n_rays);
                if (s > best_score) {
                    best_score = s;
                    best       = cand;
                }
            }
        }
    }

    out = best;
    return (int16_t)(best_score - center_score);
}
