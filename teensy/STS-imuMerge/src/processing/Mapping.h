#pragma once

// =============================================================================
//  Processing::Mapping — orchestrates the evac-zone mapping pipeline.
//
//  init():
//    Optionally restores map + pose from EEPROM, snapshots IMU yaw as θ=0,
//    brings up ToF sensors.
//
//  tick(): each call does, in order:
//    1. posePredict()    using motor command + dt
//    2. poseUpdateYaw()  from Sensors::IMU
//    3. ToF poll → log-odds integration + ray buffer for scan-match
//    4. maybeRecalibrate() + maybeCheckpoint()
//
//  handleSerial(c): debug — 'm' = ASCII map dump, 'p' = pose print.
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
