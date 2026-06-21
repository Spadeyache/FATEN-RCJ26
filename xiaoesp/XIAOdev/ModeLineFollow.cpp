#include "ModeLineFollow.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"
#include "LineCount.h"

#include <Arduino.h>
#include <math.h>

namespace {

constexpr int ARC_MAX_SAMPLES = 340;

uint8_t s_px[ARC_MAX_SAMPLES];
uint8_t s_py[ARC_MAX_SAMPLES];
uint8_t s_edge[ARC_MAX_SAMPLES];
uint8_t s_black[ARC_MAX_SAMPLES];
int     s_sampleCount = 0;
bool    s_geometryReady = false;

void addSample(int x, int y, uint8_t edge) {
    if (s_sampleCount >= ARC_MAX_SAMPLES) return;
    if (x < 0) x = 0;
    if (x > 159) x = 159;
    if (y < 0) y = 0;
    if (y > 119) y = 119;

    s_px[s_sampleCount] = (uint8_t)x;
    s_py[s_sampleCount] = (uint8_t)y;
    s_edge[s_sampleCount] = edge;
    s_sampleCount++;
}

void buildArcGeometry() {
    if (s_geometryReady) return;
    s_sampleCount = 0;

    const float cx = (float)LF_ARC_TOP_X;
    const float topY = (float)LF_ARC_TOP_Y;
    const float sideX = (float)LF_ARC_RIGHT_X;
    const float sideY = (float)LF_ARC_SIDE_Y;
    const float dx = sideX - cx;
    const float cy = (sideY * sideY - topY * topY + dx * dx) / (2.0f * (sideY - topY));
    const float r = cy - topY;

    // Closed loop order mirrors LineCount: bottom -> right side -> top arc -> left side.
    for (int x = LF_ARC_LEFT_X; x <= LF_ARC_RIGHT_X; x += LF_ARC_SAMPLE_STEP) {
        addSample(x, LF_ARC_BOTTOM_Y, LC_EDGE_BOTTOM);
    }
    for (int y = LF_ARC_BOTTOM_Y - LF_ARC_SAMPLE_STEP; y >= LF_ARC_SIDE_Y; y -= LF_ARC_SAMPLE_STEP) {
        addSample(LF_ARC_RIGHT_X, y, LC_EDGE_RIGHT);
    }
    for (int x = LF_ARC_RIGHT_X - LF_ARC_SAMPLE_STEP; x >= LF_ARC_LEFT_X; x -= LF_ARC_SAMPLE_STEP) {
        const float xdx = (float)x - cx;
        const float inside = r * r - xdx * xdx;
        const int y = (inside > 0.0f) ? (int)(cy - sqrtf(inside) + 0.5f) : LF_ARC_SIDE_Y;
        addSample(x, y, LC_EDGE_TOP);
    }
    for (int y = LF_ARC_SIDE_Y + LF_ARC_SAMPLE_STEP; y <= LF_ARC_BOTTOM_Y - LF_ARC_SAMPLE_STEP; y += LF_ARC_SAMPLE_STEP) {
        addSample(LF_ARC_LEFT_X, y, LC_EDGE_LEFT);
    }

    s_geometryReady = true;
}

void sampleArcLoop(camera_fb_t* fb) {
    for (int i = 0; i < s_sampleCount; i++) {
        cameraData d = updateRawGrayHSV(fb, s_px[i], s_py[i]);
        s_black[i] = isBlack(d) ? 1 : 0;
    }
}

void registerCrossing(LineCounts& out, int idx, int len) {
    if (out.count >= LC_MAX_CROSSINGS) return;
    Crossing& c = out.crossings[out.count++];
    c.pos = (float)idx;
    c.edge = s_edge[idx];
    c.pixelX = s_px[idx];
    c.pixelY = s_py[idx];
    c.width = (uint8_t)(len > 255 ? 255 : len);
}

void emitRun(LineCounts& out, int start, int runStartK, int len, int perimeter) {
    if (len < LC_RUN_MIN_LEN) return;
    registerCrossing(out, (start + runStartK + len / 2) % perimeter, len);
}

void detectArcCrossings(camera_fb_t* fb, LineCounts& out) {
    out.count = 0;
    out.perimeter = 0.0f;
    if (!fb || !fb->buf) return;

    buildArcGeometry();
    sampleArcLoop(fb);

    const int P = s_sampleCount;
    out.perimeter = (float)P;
    if (P == 0) return;

    int start = -1;
    for (int i = 0; i < P; i++) {
        if (!s_black[i]) { start = i; break; }
    }
    if (start < 0) {
        registerCrossing(out, P / 2, P);
        return;
    }

    int len = 0;
    int runStartK = 0;
    for (int k = 0; k < P; k++) {
        const int idx = (start + k) % P;
        if (s_black[idx]) {
            if (len == 0) runStartK = k;
            len++;
        } else if (len > 0) {
            emitRun(out, start, runStartK, len, P);
            len = 0;
        }
    }
    if (len > 0) emitRun(out, start, runStartK, len, P);
}

int selectSteeringOut(const LineCounts& lc, int inIndex) {
    int best = -1;
    int bestRank = 1000;
    int bestDx = 1000;

    for (int i = 0; i < lc.count; i++) {
        if (i == inIndex) continue;

        int rank = 2;
        if (lc.crossings[i].edge == LC_EDGE_TOP) rank = 0;
        else if (lc.crossings[i].edge == LC_EDGE_LEFT || lc.crossings[i].edge == LC_EDGE_RIGHT) rank = 1;

        int dx = (int)((float)lc.crossings[i].pixelX - LF_CENTER_X);
        if (dx < 0) dx = -dx;

        if (best < 0 || rank < bestRank || (rank == bestRank && dx < bestDx)) {
            best = i;
            bestRank = rank;
            bestDx = dx;
        }
    }
    return best;
}

uint8_t arcAngleError(const LineCounts& lc, int inIndex, int outIndex, float& angleOut, float& posOut) {
    const Crossing& in = lc.crossings[inIndex];
    const Crossing& out = lc.crossings[outIndex];

    const float dx = (float)out.pixelX - (float)in.pixelX;
    float dy = (float)in.pixelY - (float)out.pixelY;
    if (dy < 1.0f) dy = 1.0f;

    float angleDeg = atan2f(dx, dy) * 57.2957795f;
    float sideMult = 1.0f;
    if ((out.edge == LC_EDGE_LEFT || out.edge == LC_EDGE_RIGHT) && out.pixelY >= LF_ARC_SIDE_GAIN_Y) {
        sideMult = LF_ARC_SIDE_GAIN_MULT;
    }

    const float inOffset = (float)in.pixelX - LF_CENTER_X;
    float err = (float)LF_ERROR_CENTER +
        angleDeg * LF_ARC_ANGLE_SCALE * sideMult +
        inOffset * LF_ARC_IN_PX_SCALE;

    if (err < 0.0f) err = 0.0f;
    if (err > 254.0f) err = 254.0f;

    angleOut = angleDeg;
    posOut = inOffset;
    return (uint8_t)(err + 0.5f);
}

}  // namespace

void modeLineFollowRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    static uint8_t s_lastErr = LF_ERROR_CENTER;
    static float s_lastAngle = 0.0f;
    static float s_lastPos = 0.0f;

    LineCounts lc;
    detectArcCrossings(fb, lc);

    LineClass cls = lc_updateIn(lc);
    const int steerOut = selectSteeringOut(lc, cls.inIndex);

    uint8_t featureId = FEAT_NONE;
    uint8_t errByte = s_lastErr;
    bool fresh = false;

    if (cls.inIndex >= 0 && steerOut >= 0) {
        errByte = arcAngleError(lc, cls.inIndex, steerOut, s_lastAngle, s_lastPos);
        s_lastErr = errByte;
        fresh = true;
    } else if (lc.count == 0 || !cls.inHeld) {
        errByte = LF_ERROR_CENTER;
        featureId = FEAT_LINE_LOST;
        s_lastErr = errByte;
        s_lastAngle = 0.0f;
        s_lastPos = 0.0f;
    }

    lc_storeDebug(lc, cls, steerOut, errByte, s_lastAngle);
    lc_storeSteer(steerOut, false, false, 0, 0);
    row55_storeDebug(featureId == FEAT_LINE_LOST ? ROW55_WHITE : ROW55_BLACK,
                     featureId == FEAT_LINE_LOST ? ROW55_WHITE : ROW55_BLACK);

    teensy.send(XIAO_REG_FEATURE, featureId);
    teensy.send(XIAO_REG_COM, errByte);
    teensy.send(XIAO_REG_FLAG, 0);

    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=0 arc=1 feat=%d err=%d n=%d in=%d out=%d fresh=%d ang=%.1f pos=%.1f",
        featureId, errByte, lc.count, cls.inIndex, steerOut, fresh ? 1 : 0,
        s_lastAngle, s_lastPos);
}
