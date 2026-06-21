#include "ModeLineFollow.h"
#include "vision.h"
#include "config.h"
#include "serial_print.h"
#include "LineCount.h"

#include <Arduino.h>
#include <math.h>

namespace {

constexpr int ARC_MAX_SAMPLES = 340;

// Mode 0 black-line ROI:
//   top arc passes through (80,5), (25,35), (135,35)
//   side walls are x=25 and x=135, y=35..65
//   bottom line is y=65, x=25..135
constexpr uint8_t ARC_TOP_X = 80;
constexpr uint8_t ARC_TOP_Y = 5;
constexpr uint8_t ARC_LEFT_X = 25;
constexpr uint8_t ARC_RIGHT_X = 135;
constexpr uint8_t ARC_SIDE_Y = 35;
constexpr uint8_t ARC_BOTTOM_Y = 65;
constexpr uint8_t ARC_SAMPLE_STEP = 1;

// Error = 127 + signed angle * ANGLE_SCALE * side boost + bottom in-point offset * IN_PX_SCALE.
constexpr float ARC_ANGLE_SCALE = 2.0f;
constexpr float ARC_IN_PX_SCALE = 0.8f;
constexpr uint8_t ARC_SIDE_GAIN_Y = 35;
constexpr float ARC_SIDE_GAIN_MULT = 1.35f;

// Silver rescue-zone tape scan: same side-column logic as the older line/search modes.
constexpr uint8_t SILVER_COL_LEFT = 25;
constexpr uint8_t SILVER_COL_RIGHT = 135;
constexpr uint8_t SILVER_ROW_MIN = 0;
constexpr uint8_t SILVER_ROW_MAX = 70;
constexpr uint8_t SILVER_THRESHOLD = 12;

// Row-25 color processing. Bounds follow the arc ROI side walls, not the full image.
constexpr uint8_t COLOR_ROW = 25;
constexpr uint8_t COLOR_X_MIN = ARC_LEFT_X;
constexpr uint8_t COLOR_X_MAX = ARC_RIGHT_X;
constexpr uint8_t RED_THRESHOLD = 30;
constexpr uint8_t GAP_BLACK_MAX = 5;
constexpr uint8_t GREEN_WINDOW = 25;
constexpr uint8_t GREEN_LINE_HALF_W = 4;
constexpr uint8_t GREEN_PX_THRESHOLD = 5;

// Green command filter, local to mode 0. raw/command: 0 none, 1 u-turn, 2 left, 3 right.
constexpr uint8_t GREEN_QUEUE_SIZE = 15;
constexpr uint8_t GREEN_SIDE_VOTES = 4;
constexpr uint8_t GREEN_UTURN_VOTES = 2;
constexpr uint8_t GREEN_BOTH_VOTES = 4;

// During a black-saturated intersection row, ignore/reset green votes so green
// after the intersection does not accidentally command a turn.
constexpr uint8_t INTERSECTION_BLACK_SAT_THRESHOLD = 35;

// Green-left/right commit target. Ends after the branch has been seen and the
// line settles back to exactly two crossings (in + one out) for this many frames.
constexpr uint8_t COMMIT_SETTLE_FRAMES = 3;
constexpr uint8_t COMMIT_SIDE_MARGIN = 10;
constexpr float COMMIT_TRACK_GATE = 50.0f;

uint8_t s_px[ARC_MAX_SAMPLES];
uint8_t s_py[ARC_MAX_SAMPLES];
uint8_t s_edge[ARC_MAX_SAMPLES];
uint8_t s_black[ARC_MAX_SAMPLES];
int     s_sampleCount = 0;
bool    s_geometryReady = false;

uint8_t s_greenQ[GREEN_QUEUE_SIZE] = {};
uint8_t s_greenHead = 0;
bool    s_commitActive = false;
bool    s_commitLeft = false;
bool    s_commitSeenBranch = false;
bool    s_commitLocked = false;
float   s_commitLockPos = 0.0f;
uint8_t s_commitSettle = 0;
uint8_t s_lastErr = LF_ERROR_CENTER;
float   s_lastAngle = 0.0f;
float   s_lastPos = 0.0f;

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

    const float cx = (float)ARC_TOP_X;
    const float topY = (float)ARC_TOP_Y;
    const float sideX = (float)ARC_RIGHT_X;
    const float sideY = (float)ARC_SIDE_Y;
    const float dx = sideX - cx;
    const float cy = (sideY * sideY - topY * topY + dx * dx) / (2.0f * (sideY - topY));
    const float r = cy - topY;

    // Closed loop order mirrors LineCount: bottom -> right side -> top arc -> left side.
    for (int x = ARC_LEFT_X; x <= ARC_RIGHT_X; x += ARC_SAMPLE_STEP) {
        addSample(x, ARC_BOTTOM_Y, LC_EDGE_BOTTOM);
    }
    for (int y = ARC_BOTTOM_Y - ARC_SAMPLE_STEP; y >= ARC_SIDE_Y; y -= ARC_SAMPLE_STEP) {
        addSample(ARC_RIGHT_X, y, LC_EDGE_RIGHT);
    }
    for (int x = ARC_RIGHT_X - ARC_SAMPLE_STEP; x >= ARC_LEFT_X; x -= ARC_SAMPLE_STEP) {
        const float xdx = (float)x - cx;
        const float inside = r * r - xdx * xdx;
        const int y = (inside > 0.0f) ? (int)(cy - sqrtf(inside) + 0.5f) : ARC_SIDE_Y;
        addSample(x, y, LC_EDGE_TOP);
    }
    for (int y = ARC_SIDE_Y + ARC_SAMPLE_STEP; y <= ARC_BOTTOM_Y - ARC_SAMPLE_STEP; y += ARC_SAMPLE_STEP) {
        addSample(ARC_LEFT_X, y, LC_EDGE_LEFT);
    }

    s_geometryReady = true;
}

void sampleArcLoop(camera_fb_t* fb) {
    for (int i = 0; i < s_sampleCount; i++) {
        cameraData d = updateRawGrayHSV(fb, s_px[i], s_py[i]);
        s_black[i] = isBlack(d) ? 1 : 0;
    }
}

uint8_t countSilverOnColumn(camera_fb_t* fb, uint8_t col) {
    uint8_t count = 0;
    for (uint8_t y = SILVER_ROW_MIN; y <= SILVER_ROW_MAX; y++) {
        if (isSilver(updateRawGrayHSV(fb, col, y))) count++;
    }
    return count;
}

void scanColorRow(camera_fb_t* fb, float& blackCom, uint8_t& blackCount, uint8_t& redCount) {
    cameraData rowPixels[160] = {};
    scanRow(fb, COLOR_ROW, COLOR_X_MIN, COLOR_X_MAX, rowPixels);

    int32_t weighted = 0;
    blackCount = 0;
    redCount = 0;
    for (uint8_t x = COLOR_X_MIN; x <= COLOR_X_MAX; x++) {
        if (isBlack(rowPixels[x])) { weighted += x; blackCount++; }
        if (isRed(rowPixels[x])) redCount++;
    }
    blackCom = blackCount ? (float)weighted / blackCount : (COLOR_X_MIN + COLOR_X_MAX) * 0.5f;
}

uint8_t rawGreenOnColorRow(camera_fb_t* fb, float lineCom, uint8_t& greenLeft, uint8_t& greenRight) {
    cameraData rowPixels[160] = {};
    scanRow(fb, COLOR_ROW, COLOR_X_MIN, COLOR_X_MAX, rowPixels);

    const int16_t comI = (int16_t)lineCom;
    const int16_t leftEndS = comI - GREEN_LINE_HALF_W;
    const int16_t rightStartS = comI + GREEN_LINE_HALF_W;
    const uint8_t leftEnd = (leftEndS > COLOR_X_MIN) ? (uint8_t)leftEndS : COLOR_X_MIN;
    const uint8_t leftStart = (leftEnd > COLOR_X_MIN + GREEN_WINDOW) ? (uint8_t)(leftEnd - GREEN_WINDOW) : COLOR_X_MIN;
    const uint8_t rightStart = (rightStartS < COLOR_X_MAX) ? (uint8_t)rightStartS : COLOR_X_MAX;
    const uint8_t rightEnd = (rightStart + GREEN_WINDOW > COLOR_X_MAX) ? COLOR_X_MAX : (uint8_t)(rightStart + GREEN_WINDOW);

    greenLeft = 0;
    greenRight = 0;
    for (uint8_t x = leftStart; x < leftEnd; x++) {
        if (isGreen(rowPixels[x])) greenLeft++;
    }
    for (uint8_t x = rightStart; x < rightEnd; x++) {
        if (isGreen(rowPixels[x])) greenRight++;
    }

    const bool left = greenLeft > GREEN_PX_THRESHOLD;
    const bool right = greenRight > GREEN_PX_THRESHOLD;
    if (left && right) return 1;
    if (left) return 2;
    if (right) return 3;
    return 0;
}

void resetGreenFilter() {
    for (uint8_t i = 0; i < GREEN_QUEUE_SIZE; i++) s_greenQ[i] = 0;
    s_greenHead = 0;
}

uint8_t updateGreenFilter(uint8_t raw) {
    s_greenQ[s_greenHead] = raw;
    s_greenHead = (uint8_t)((s_greenHead + 1) % GREEN_QUEUE_SIZE);

    uint8_t vL = 0, vR = 0, vU = 0;
    for (uint8_t i = 0; i < GREEN_QUEUE_SIZE; i++) {
        switch (s_greenQ[i]) {
            case 1: vU++; vL++; vR++; break;
            case 2: vL++; break;
            case 3: vR++; break;
            default: break;
        }
    }

    if (vU >= GREEN_UTURN_VOTES || (vL >= GREEN_BOTH_VOTES && vR >= GREEN_BOTH_VOTES)) return 1;
    if (vL >= GREEN_SIDE_VOTES) return 2;
    if (vR >= GREEN_SIDE_VOTES) return 3;
    return 0;
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

void clearCommit() {
    s_commitActive = false;
    s_commitSeenBranch = false;
    s_commitLocked = false;
    s_commitSettle = 0;
}

void startCommit(bool left) {
    s_commitActive = true;
    s_commitLeft = left;
    s_commitSeenBranch = false;
    s_commitLocked = false;
    s_commitSettle = 0;
}

int committedOut(const LineCounts& lc, int inIndex) {
    if (!s_commitActive || inIndex < 0) return -1;

    if (!s_commitLocked) {
        int best = -1;
        for (int i = 0; i < lc.count; i++) {
            if (i == inIndex) continue;
            const float x = (float)lc.crossings[i].pixelX;
            const bool onSide = s_commitLeft ? (x <= LF_CENTER_X - COMMIT_SIDE_MARGIN)
                                             : (x >= LF_CENTER_X + COMMIT_SIDE_MARGIN);
            if (!onSide) continue;
            if (best < 0 ||
                (s_commitLeft && x < (float)lc.crossings[best].pixelX) ||
                (!s_commitLeft && x > (float)lc.crossings[best].pixelX)) {
                best = i;
            }
        }
        if (best >= 0) {
            s_commitLocked = true;
            s_commitLockPos = lc.crossings[best].pos;
        }
        return best;
    }

    int best = -1;
    float bestDist = 1e9f;
    for (int i = 0; i < lc.count; i++) {
        if (i == inIndex) continue;
        const float d = lc_loopDist(s_commitLockPos, lc.crossings[i].pos, lc.perimeter);
        if (d < bestDist) {
            best = i;
            bestDist = d;
        }
    }
    if (best >= 0 && bestDist <= COMMIT_TRACK_GATE) {
        s_commitLockPos = lc.crossings[best].pos;
        return best;
    }
    return -1;
}

void updateCommitSettle(const LineClass& cls, uint8_t crossingCount) {
    if (!s_commitActive) return;

    if (cls.outCount >= 2) {
        s_commitSeenBranch = true;
        s_commitSettle = 0;
        return;
    }

    if (s_commitSeenBranch && crossingCount == 2 && cls.outCount == 1) {
        if (++s_commitSettle >= COMMIT_SETTLE_FRAMES) clearCommit();
    } else {
        s_commitSettle = 0;
    }
}

uint8_t arcAngleError(const LineCounts& lc, int inIndex, int outIndex, float& angleOut, float& posOut) {
    const Crossing& in = lc.crossings[inIndex];
    const Crossing& out = lc.crossings[outIndex];

    const float dx = (float)out.pixelX - (float)in.pixelX;
    float dy = (float)in.pixelY - (float)out.pixelY;
    if (dy < 1.0f) dy = 1.0f;

    float angleDeg = atan2f(dx, dy) * 57.2957795f;
    float sideMult = 1.0f;
    if ((out.edge == LC_EDGE_LEFT || out.edge == LC_EDGE_RIGHT) && out.pixelY >= ARC_SIDE_GAIN_Y) {
        sideMult = ARC_SIDE_GAIN_MULT;
    }

    const float inOffset = (float)in.pixelX - LF_CENTER_X;
    float err = (float)LF_ERROR_CENTER +
        angleDeg * ARC_ANGLE_SCALE * sideMult +
        inOffset * ARC_IN_PX_SCALE;

    if (err < 0.0f) err = 0.0f;
    if (err > 254.0f) err = 254.0f;

    angleOut = angleDeg;
    posOut = inOffset;
    return (uint8_t)(err + 0.5f);
}

}  // namespace

void modeLineFollowReset() {
    resetGreenFilter();
    clearCommit();
    lc_resetTracking();
    s_lastErr = LF_ERROR_CENTER;
    s_lastAngle = 0.0f;
    s_lastPos = 0.0f;
}

void modeLineFollowRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    LineCounts lc;
    detectArcCrossings(fb, lc);

    LineClass cls = lc_updateIn(lc);
    const int normalSteerOut = selectSteeringOut(lc, cls.inIndex);
    const uint8_t silverLeft = countSilverOnColumn(fb, SILVER_COL_LEFT);
    const uint8_t silverRight = countSilverOnColumn(fb, SILVER_COL_RIGHT);
    const bool silverDetected = silverLeft > SILVER_THRESHOLD || silverRight > SILVER_THRESHOLD;
    float colorCom;
    uint8_t colorBlack, redCount;
    scanColorRow(fb, colorCom, colorBlack, redCount);
    const bool gapDetected = colorBlack <= GAP_BLACK_MAX;
    const bool intersectionSaturated = colorBlack > INTERSECTION_BLACK_SAT_THRESHOLD;

    uint8_t greenLeft = 0, greenRight = 0;
    uint8_t rawGreen = rawGreenOnColorRow(fb, colorCom, greenLeft, greenRight);
    uint8_t greenCmd = 0;
    if (intersectionSaturated || gapDetected) {
        resetGreenFilter();
        rawGreen = 0;
    } else if (!s_commitActive) {
        greenCmd = updateGreenFilter(rawGreen);
        if (greenCmd != 0) resetGreenFilter();
        if (greenCmd == 1) clearCommit();
        if (greenCmd == 2) startCommit(true);
        if (greenCmd == 3) startCommit(false);
    }

    uint8_t featureId = FEAT_NONE;
    uint8_t errByte = s_lastErr;
    bool fresh = false;
    int steerOut = s_commitActive ? committedOut(lc, cls.inIndex) : normalSteerOut;

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

    if (greenCmd == 1) featureId = FEAT_UTURN;
    if (gapDetected) featureId = FEAT_LINE_LOST;
    if (redCount > RED_THRESHOLD) featureId = FEAT_RED;
    if (silverDetected) featureId = FEAT_SILVER;
    updateCommitSettle(cls, lc.count);

    lc_storeDebug(lc, cls, steerOut, errByte, s_lastAngle);
    lc_storeArcRoi(ARC_TOP_X, ARC_TOP_Y,
                   ARC_LEFT_X, ARC_SIDE_Y,
                   ARC_RIGHT_X, ARC_SIDE_Y,
                   ARC_LEFT_X, ARC_BOTTOM_Y,
                   ARC_RIGHT_X, ARC_BOTTOM_Y);
    lc_storeSteer(steerOut, s_commitActive, s_commitLocked, s_commitSettle, greenCmd);
    const uint8_t rowClass =
        (featureId == FEAT_RED) ? ROW55_RED :
        (featureId == FEAT_SILVER) ? ROW55_SILVER :
        ((greenLeft > GREEN_PX_THRESHOLD || greenRight > GREEN_PX_THRESHOLD) ? ROW55_GREEN :
        (featureId == FEAT_LINE_LOST ? ROW55_WHITE : ROW55_BLACK));
    row55_storeDebug(rowClass, rowClass);

    teensy.send(XIAO_REG_FEATURE, featureId);
    teensy.send(XIAO_REG_COM, errByte);
    teensy.send(XIAO_REG_FLAG, s_commitActive ? 1 : 0);

    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=0 arc=1 feat=%d err=%d n=%d in=%d out=%d fresh=%d ang=%.1f pos=%.1f sL=%d sR=%d red=%d blk25=%d gap=%d sat=%d gL=%d gR=%d rawG=%d gc=%d cmt=%d",
        featureId, errByte, lc.count, cls.inIndex, steerOut, fresh ? 1 : 0,
        s_lastAngle, s_lastPos, silverLeft, silverRight, redCount, colorBlack,
        gapDetected ? 1 : 0, intersectionSaturated ? 1 : 0, greenLeft, greenRight,
        rawGreen, greenCmd, s_commitActive ? 1 : 0);
}
