#pragma once

// =============================================================================
//  Processing::Mapping — evac-zone pose estimator (Phase 0).
//
//  init():  pose at the entrance, snapshots IMU yaw as θ=0, brings up ToF.
//
//  tick(): each call does, in order:
//    1. posePredict()    using motor command + dt
//    2. poseUpdateYaw()  from Sensors::IMU
//
//  handleSerial(c): debug — 'p' = pose print, 't' = raw ToF distance dump.
//
//  Removed in Phase 0 (to be re-added incrementally): EEPROM persistence,
//  occupancy grid + ToF integration, and scan-match recalibration.
// =============================================================================

#include "Pose.h"

namespace Processing {
namespace Mapping {

void init(bool restart = false);
void tick();
void persist();              // force EEPROM save (call on exit)
void handleSerial(char c);

const Pose& pose();          // read-only access for state-machine logging

}  // namespace Mapping
}  // namespace Processing
