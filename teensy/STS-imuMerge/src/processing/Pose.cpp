#include "Pose.h"
#include "../../config.h"
#include <math.h>

float wrapAngle(float a) {
    while (a >  (float)PI) a -= 2.0f * (float)PI;
    while (a < -(float)PI) a += 2.0f * (float)PI;
    return a;
}

void poseInit(Pose& p,
              float x_mm, float y_mm, float theta_rad,
              float sigma_xy_mm, float sigma_theta_rad) {
    p.x_mm  = x_mm;
    p.y_mm  = y_mm;
    p.theta = wrapAngle(theta_rad);
    for (int i = 0; i < 9; ++i) p.P[i] = 0.0f;
    const float vxy = sigma_xy_mm * sigma_xy_mm;
    const float vth = sigma_theta_rad * sigma_theta_rad;
    p.P[0] = vxy;
    p.P[4] = vxy;
    p.P[8] = vth;
}

void poseInflateCovariance(Pose& p, float dxy_mm, float dtheta_rad) {
    p.P[0] += dxy_mm * dxy_mm;
    p.P[4] += dxy_mm * dxy_mm;
    p.P[8] += dtheta_rad * dtheta_rad;
}

// Predict: x' = f(x, u) with linearised F P F^T + Q.
void posePredict(Pose& p, float v_mm_s, float omega_rad_s, float dt_s) {
    if (dt_s <= 0.0f) return;

    const float c   = cosf(p.theta);
    const float s   = sinf(p.theta);
    const float vdt = v_mm_s * dt_s;

    p.x_mm  += vdt * c;
    p.y_mm  += vdt * s;
    p.theta  = wrapAngle(p.theta + omega_rad_s * dt_s);

    // F = I + delta where delta only has F[0][2] = -v s dt and F[1][2] = v c dt.
    const float Fa = -vdt * s;
    const float Fb =  vdt * c;

    float M[9];
    M[0] = p.P[0] + Fa * p.P[6];
    M[1] = p.P[1] + Fa * p.P[7];
    M[2] = p.P[2] + Fa * p.P[8];
    M[3] = p.P[3] + Fb * p.P[6];
    M[4] = p.P[4] + Fb * p.P[7];
    M[5] = p.P[5] + Fb * p.P[8];
    M[6] = p.P[6];
    M[7] = p.P[7];
    M[8] = p.P[8];

    p.P[0] = M[0];
    p.P[1] = M[1];
    p.P[2] = M[0] * Fa + M[1] * Fb + M[2];
    p.P[3] = M[3];
    p.P[4] = M[4];
    p.P[5] = M[3] * Fa + M[4] * Fb + M[5];
    p.P[6] = M[6];
    p.P[7] = M[7];
    p.P[8] = M[6] * Fa + M[7] * Fb + M[8];

    const float sv  = EKF_Q_V_FRAC * fabsf(v_mm_s) * dt_s;
    const float sw  = EKF_Q_OMEGA  * dt_s;
    const float sth = EKF_Q_THETA_RAD * dt_s;
    p.P[0] += sv * sv + 1e-3f;
    p.P[4] += sv * sv + 1e-3f;
    p.P[8] += sw * sw + sth * sth + 1e-6f;
}

void poseUpdateYaw(Pose& p, float yaw_rad_meas) {
    const float R = EKF_R_YAW_RAD2;

    const float innov = wrapAngle(yaw_rad_meas - p.theta);
    const float S     = p.P[8] + R;
    if (S <= 0.0f) return;

    const float K0 = p.P[2] / S;
    const float K1 = p.P[5] / S;
    const float K2 = p.P[8] / S;

    p.x_mm  += K0 * innov;
    p.y_mm  += K1 * innov;
    p.theta  = wrapAngle(p.theta + K2 * innov);

    const float row2_0 = p.P[6];
    const float row2_1 = p.P[7];
    const float row2_2 = p.P[8];
    p.P[0] -= K0 * row2_0;
    p.P[1] -= K0 * row2_1;
    p.P[2] -= K0 * row2_2;
    p.P[3] -= K1 * row2_0;
    p.P[4] -= K1 * row2_1;
    p.P[5] -= K1 * row2_2;
    p.P[6] -= K2 * row2_0;
    p.P[7] -= K2 * row2_1;
    p.P[8] -= K2 * row2_2;
}

void poseUpdateWall(Pose& p, float wall_angle_rad, float wall_d_mm) {
    const float c = cosf(wall_angle_rad);
    const float s = sinf(wall_angle_rad);
    const float R = EKF_R_WALL_MM2;

    const float predicted = p.x_mm * c + p.y_mm * s;
    const float innov     = wall_d_mm - predicted;

    const float S = p.P[0] * c * c + 2.0f * p.P[1] * c * s + p.P[4] * s * s + R;
    if (S <= 0.0f) return;

    const float K0 = (p.P[0] * c + p.P[1] * s) / S;
    const float K1 = (p.P[3] * c + p.P[4] * s) / S;
    const float K2 = (p.P[6] * c + p.P[7] * s) / S;

    p.x_mm  += K0 * innov;
    p.y_mm  += K1 * innov;
    p.theta  = wrapAngle(p.theta + K2 * innov);

    const float h0 = c * p.P[0] + s * p.P[3];
    const float h1 = c * p.P[1] + s * p.P[4];
    const float h2 = c * p.P[2] + s * p.P[5];

    p.P[0] -= K0 * h0;  p.P[1] -= K0 * h1;  p.P[2] -= K0 * h2;
    p.P[3] -= K1 * h0;  p.P[4] -= K1 * h1;  p.P[5] -= K1 * h2;
    p.P[6] -= K2 * h0;  p.P[7] -= K2 * h1;  p.P[8] -= K2 * h2;
}
