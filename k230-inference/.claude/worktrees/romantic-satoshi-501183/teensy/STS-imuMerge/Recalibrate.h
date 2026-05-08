#ifndef Recalibrate_h
#define Recalibrate_h

// =============================================================================
//  Recalibrate — pose recovery via stuck-detection + ToF↔map scan matching.
//
//  Two pieces:
//    1. StuckDetector — call updateStuck() every tick with current pose
//       and commanded velocity; returns true when motion has stalled despite
//       a non-zero command for RECAL_STUCK_MS.
//
//    2. scanMatchPose() — 3-D grid search over (Δx, Δy, Δθ) within
//       [±RECAL_WIN_XY_MM, ±RECAL_WIN_TH_DEG]. For each candidate, rotates+
//       translates the latest ToF endpoints and scores them against the
//       map's existing semantic. Returns the best-scoring pose; caller
//       decides whether to accept based on the margin.
//
//  Phase 1 wires this into Mapping.ino's maybeRecalibrate(). Early on the
//  map is mostly UNKNOWN so scores are low and the snap is rarely accepted —
//  that's fine; the trigger has minimal cost when the map is empty.
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "Pose.h"
#include "MapGrid.h"

class StuckDetector {
public:
    void reset();
    // Returns true once when stuck condition has been continuously held for
    // RECAL_STUCK_MS. Re-arms on the next reset() or motion.
    bool updateStuck(const Pose& p, float v_cmd_mm_s);

private:
    bool          _armed     = false;
    uint32_t      _arm_ms    = 0;
    float         _arm_x_mm  = 0.0f;
    float         _arm_y_mm  = 0.0f;
};

// Sensor reading container for scan-match input.
struct ScanRay {
    float bearing_local;   // sensor-frame horizontal bearing (rad)
    float range_mm;        // valid endpoint range
    // mount transform for the sensor that produced this ray:
    float mount_dx, mount_dy, mount_yaw;
};

// Score a pose against the map given a set of endpoint rays.
// score = +1 per ray whose endpoint cell is CS_WALL, -1 if CS_FREE,
//          0 if CS_UNKNOWN, +2 for matching specific obstacle/evac semantics.
int16_t scoreScan(const EvacMap& m,
                  const Pose&    candidate,
                  const ScanRay* rays,
                  uint16_t       n_rays);

// Search around `seed`. Writes best pose to `out`. Returns the score margin
// (best - center). Caller compares against RECAL_MARGIN_MIN before snapping.
int16_t scanMatchPose(const EvacMap& m,
                      const Pose&    seed,
                      const ScanRay* rays,
                      uint16_t       n_rays,
                      Pose&          out);

#endif
