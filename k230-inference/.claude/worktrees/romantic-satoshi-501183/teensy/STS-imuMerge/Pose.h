#ifndef Pose_h
#define Pose_h

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
//
//  Pure-C++ (no Arduino), so the math can be unit-tested off-target.
//  Hand-rolled 3x3 ops are faster and clearer than CMSIS-DSP for this size.
// =============================================================================

#include <arm_math.h>

struct Pose {
    float x_mm;
    float y_mm;
    float theta;     // radians, [-π, π]
    float P[9];      // 3x3 covariance, row-major
};

// --- Construction / reset ---
void poseInit(Pose& p,
              float x_mm, float y_mm, float theta_rad,
              float sigma_xy_mm = 5.0f,
              float sigma_theta_rad = 0.02f);

// --- Prediction step ---
//   v_mm_s     : commanded forward velocity (signed, +x in robot frame)
//   omega_rad_s: commanded angular velocity (CCW positive)
//   dt_s       : seconds since previous predict
void posePredict(Pose& p, float v_mm_s, float omega_rad_s, float dt_s);

// --- Measurement update: IMU yaw (scalar, R = EKF_R_YAW_RAD2) ---
void poseUpdateYaw(Pose& p, float yaw_rad_meas);

// --- Measurement update: wall-snap (scalar, R = EKF_R_WALL_MM2) ---
//   wall_angle_rad : world-frame normal angle of the fitted wall (radians)
//   wall_d_mm      : signed perpendicular distance from world origin to wall
//   Measurement model: d = x cos(α) + y sin(α) + n
//   H = [cos(α), sin(α), 0]
void poseUpdateWall(Pose& p, float wall_angle_rad, float wall_d_mm);

// --- Helpers ---
float wrapAngle(float a);  // to [-π, π]

// Diagonal-only inflation, useful after a scan-match snap.
void poseInflateCovariance(Pose& p, float dxy_mm, float dtheta_rad);

#endif
