#pragma once

// =============================================================================
//  Pose — 3-state EKF for (x_mm, y_mm, theta_rad) in robot-frame.
//
//  Covariance P is 3x3 row-major:
//      P[0] P[1] P[2]
//      P[3] P[4] P[5]
//      P[6] P[7] P[8]
//
//  Phase 1 fuses:
//    1. posePredict()       — motor command (v, omega) integration
//    2. poseUpdateYaw()     — IMU yaw measurement (Madgwick)
//
//  Phase 2 will add poseUpdateWall() for ToF wall-snap correction.
// =============================================================================

#include <arm_math.h>

struct Pose {
    float x_mm;
    float y_mm;
    float theta;     // radians, [-π, π]
    float P[9];      // 3x3 covariance, row-major
};

void poseInit(Pose& p,
              float x_mm, float y_mm, float theta_rad,
              float sigma_xy_mm = 5.0f,
              float sigma_theta_rad = 0.02f);

void posePredict(Pose& p, float v_mm_s, float omega_rad_s, float dt_s);
void poseUpdateYaw (Pose& p, float yaw_rad_meas);
void poseUpdateWall(Pose& p, float wall_angle_rad, float wall_d_mm);

float wrapAngle(float a);   // → [-π, π]
void  poseInflateCovariance(Pose& p, float dxy_mm, float dtheta_rad);
