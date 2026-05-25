#pragma once

// =============================================================================
//  Recalibrate — pose recovery via stuck-detection + ToF↔map scan matching.
//
//  1. StuckDetector  — true once motion has stalled despite a forward command
//                      for RECAL_STUCK_MS.
//  2. scanMatchPose() — grid search over (Δx, Δy, Δθ) around `seed`, scoring
//                      each candidate against the current map. Returns the
//                      best-scoring pose and the margin over the seed score.
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "Pose.h"
#include "MapGrid.h"

class StuckDetector {
public:
    void reset();
    bool updateStuck(const Pose& p, float v_cmd_mm_s);

private:
    bool          _armed     = false;
    uint32_t      _arm_ms    = 0;
    float         _arm_x_mm  = 0.0f;
    float         _arm_y_mm  = 0.0f;
};

struct ScanRay {
    float bearing_local;
    float range_mm;
    float mount_dx, mount_dy, mount_yaw;
};

int16_t scoreScan(const EvacMap& m,
                  const Pose&    candidate,
                  const ScanRay* rays,
                  uint16_t       n_rays);

int16_t scanMatchPose(const EvacMap& m,
                      const Pose&    seed,
                      const ScanRay* rays,
                      uint16_t       n_rays,
                      Pose&          out);
