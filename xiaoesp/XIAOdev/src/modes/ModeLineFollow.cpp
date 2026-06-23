#include "ModeLineFollow.h"
#include "../processing/vision.h"
#include "../config/config.h"
#include "../config/serial_print.h"
#include "../processing/LineCount.h"
#include "../stream/XiaoStream.h"

#include <Arduino.h>
#include <math.h>

namespace {

constexpr int ARC_MAX_SAMPLES = 340;


//run this to update serial
//powershell -ExecutionPolicy Bypass -File xiaoesp\XIAOdev\src\stream\update_stream_guides.ps1

// Mode 0 black-line ROI:
//   top arc passes through (80,5), (25,35), (135,35)
constexpr uint8_t ARC_TOP_X = 80;
constexpr uint8_t ARC_TOP_Y = 3;
constexpr uint8_t ARC_LEFT_X = 25;
constexpr uint8_t ARC_RIGHT_X = 135;
constexpr uint8_t ARC_SIDE_Y = 33;
constexpr uint8_t ARC_BOTTOM_Y = 85; //85 i might need to change the IN_PX gain
constexpr uint8_t ARC_BOTTOM_LEFT_X = 40;
constexpr uint8_t ARC_BOTTOM_RIGHT_X = 120;
constexpr uint8_t ARC_SAMPLE_STEP = 1;
constexpr uint8_t BOUNDARY_BLACK_NEIGHBOR_RADIUS = 2;
constexpr uint8_t BOUNDARY_BLACK_GAP_FILL = 5;
constexpr uint8_t BOUNDARY_GAP_FILL_Y_MAX = 60;

// Error = 127 + signed angle * ANGLE_SCALE * side boost + bottom in-point offset * IN_PX_SCALE.
constexpr float ARC_ANGLE_SCALE = 2.0f; //2.0 
constexpr float ARC_IN_PX_SCALE = 0.8f; // balance of front and back gain
constexpr uint8_t ARC_SIDE_GAIN_Y = 40;  //boosts with gain in the side bellow Y : for tight turns
constexpr float ARC_SIDE_GAIN_MULT = 1.60f; //1.8
constexpr uint8_t TIGHT_SLOW_OUT_Y = 65;
constexpr uint8_t SINGLE_FRONT_ROW_DY = 12;
constexpr uint8_t SINGLE_FRONT_ROW_HALF_W = 30;
constexpr uint8_t SINGLE_FRONT_ROW_MIN_BLACK = 3;

// Silver rescue-zone tape scan: same side-column logic as the older line/search modes.
constexpr uint8_t SILVER_COL_LEFT = 33;
constexpr uint8_t SILVER_COL_RIGHT = 127;
constexpr uint8_t SILVER_ROW_MIN = 0;
constexpr uint8_t SILVER_ROW_MAX = 70;
constexpr uint8_t SILVER_THRESHOLD = 6; //num of px

// Row-25 color processing. Bounds follow the arc ROI side walls, not the full image.
constexpr uint8_t COLOR_ROW = 65;
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
constexpr uint8_t INTERSECTION_BLACK_SAT_THRESHOLD = 60;

// Green-left/right commit target. Ends after the branch has been seen and the
// line settles back to exactly two crossings (in + one out) for this many frames.
constexpr uint8_t COMMIT_SETTLE_FRAMES = 3;
// constexpr uint8_t COMMIT_SIDE_MARGIN = 10;
// constexpr float COMMIT_TRACK_GATE = 50.0f;
// COMMIT_SIDE_MARGIN and COMMIT_TRACK_GATE are defined in config.h

// Curve commit (auto, no green): when a clean 1-in/1-out frame has the out
// already in the side-boost (tight-turn) zone, lock that out as the steering
// target and ride it through the turn — so the return-stub (はみだし) that shows
// up on the far side mid-turn can't steal the target. Unlike the green commit it
// raises NO Teensy flag and does NOT touch the green filter. It releases once the
// in->out slope has calmed (line straightened) for a few frames.
constexpr float   CURVE_CALM_ANGLE_DEG = 18.0f;  // |in->out angle| below this = straightened
constexpr uint8_t CURVE_CALM_FRAMES    = 2;      // consecutive calm frames to release
constexpr uint8_t CURVE_LOST_FRAMES    = 6;      // release if the committed out is unseen this long

// Viewer-only width threshold: out points this wide are drawn magenta.
constexpr uint8_t HAMIDASHI_OUT_WIDTH_MIN = 12;

// If the forward arc/side ROI contains about two lines worth of black,
// treat it as saturated/intersection-like and drive straight instead of chasing an out point.
constexpr uint16_t ARC_BLACK_CENTER_THRESHOLD = 50;

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
bool    s_curveActive = false;
float   s_curveLockPos = 0.0f;
uint8_t s_curveCalm = 0;
uint8_t s_curveLost = 0;
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

bool isBlackPixelAt(camera_fb_t* fb, int x, int y) {
    if (!fb || !fb->buf) return false;
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return false;

    uint8_t r, g, b;
    rgb565To888(unpackRGB565(fb->buf, y * fb->width + x), r, g, b);
    rgb888Calibration(r, g, b);
    return rgbToGray(r, g, b) <= BLACK_GRAY_MAX;
}

bool neighborhoodBlack(camera_fb_t* fb, uint8_t x, uint8_t y) {
    const int r = BOUNDARY_BLACK_NEIGHBOR_RADIUS;
    return isBlackPixelAt(fb, x, y) ||
           isBlackPixelAt(fb, (int)x - r, y) ||
           isBlackPixelAt(fb, (int)x + r, y) ||
           isBlackPixelAt(fb, x, (int)y - r) ||
           isBlackPixelAt(fb, x, (int)y + r) ||
           isBlackPixelAt(fb, (int)x - r, (int)y - r) ||
           isBlackPixelAt(fb, (int)x + r, (int)y - r) ||
           isBlackPixelAt(fb, (int)x - r, (int)y + r) ||
           isBlackPixelAt(fb, (int)x + r, (int)y + r);
}

bool boundarySampleBlack(camera_fb_t* fb, uint8_t x, uint8_t y) {
    return neighborhoodBlack(fb, x, y);
}

bool gapFillAllowedAt(int idx) {
    return s_edge[idx] != LC_EDGE_BOTTOM && s_py[idx] < BOUNDARY_GAP_FILL_Y_MAX;
}

void closeBoundaryBlackGaps() {
    const int P = s_sampleCount;
    if (P <= 0) return;

    uint8_t fill[ARC_MAX_SAMPLES] = {};
    for (int i = 0; i < P; i++) {
        if (s_black[i] || !gapFillAllowedAt(i)) continue;

        for (int gap = 1; gap <= BOUNDARY_BLACK_GAP_FILL; gap++) {
            bool allGap = true;
            for (int k = 0; k < gap; k++) {
                const int idx = (i + k) % P;
                if (s_black[idx] || !gapFillAllowedAt(idx)) {
                    allGap = false;
                    break;
                }
            }
            if (!allGap) break;

            const int before = (i - 1 + P) % P;
            const int after = (i + gap) % P;
            if (gapFillAllowedAt(before) && gapFillAllowedAt(after) &&
                s_black[before] && s_black[after]) {
                for (int k = 0; k < gap; k++) fill[(i + k) % P] = 1;
                break;
            }
        }
    }

    for (int i = 0; i < P; i++) {
        if (fill[i]) s_black[i] = 1;
    }
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
    for (int x = ARC_BOTTOM_LEFT_X; x <= ARC_BOTTOM_RIGHT_X; x += ARC_SAMPLE_STEP) {
        addSample(x, ARC_BOTTOM_Y, LC_EDGE_BOTTOM);
    }
    for (int y = ARC_BOTTOM_Y - ARC_SAMPLE_STEP; y >= ARC_SIDE_Y; y -= ARC_SAMPLE_STEP) {
        const int dy = ARC_BOTTOM_Y - y;
        const int span = ARC_BOTTOM_Y - ARC_SIDE_Y;
        const int x = ARC_BOTTOM_RIGHT_X + ((ARC_RIGHT_X - ARC_BOTTOM_RIGHT_X) * dy + span / 2) / span;
        addSample(x, y, LC_EDGE_RIGHT);
    }
    for (int x = ARC_RIGHT_X - ARC_SAMPLE_STEP; x >= ARC_LEFT_X; x -= ARC_SAMPLE_STEP) {
        const float xdx = (float)x - cx;
        const float inside = r * r - xdx * xdx;
        const int y = (inside > 0.0f) ? (int)(cy - sqrtf(inside) + 0.5f) : ARC_SIDE_Y;
        addSample(x, y, LC_EDGE_TOP);
    }
    for (int y = ARC_SIDE_Y + ARC_SAMPLE_STEP; y <= ARC_BOTTOM_Y - ARC_SAMPLE_STEP; y += ARC_SAMPLE_STEP) {
        const int dy = y - ARC_SIDE_Y;
        const int span = ARC_BOTTOM_Y - ARC_SIDE_Y;
        const int x = ARC_LEFT_X + ((ARC_BOTTOM_LEFT_X - ARC_LEFT_X) * dy + span / 2) / span;
        addSample(x, y, LC_EDGE_LEFT);
    }

    s_geometryReady = true;
}

void sampleArcLoop(camera_fb_t* fb) {
    for (int i = 0; i < s_sampleCount; i++) {
        s_black[i] = boundarySampleBlack(fb, s_px[i], s_py[i]) ? 1 : 0;
    }
    closeBoundaryBlackGaps();
}

uint16_t countArcBlackSamples() {
    uint16_t count = 0;
    for (int i = 0; i < s_sampleCount; i++) {
        if (s_edge[i] != LC_EDGE_BOTTOM && s_black[i]) count++;
    }
    return count;
}

uint8_t countSilverOnColumn(camera_fb_t* fb, uint8_t col) {
    uint8_t count = 0;
    for (uint8_t y = SILVER_ROW_MIN; y <= SILVER_ROW_MAX; y++) {
        RawRgb px;
        if (sampleRawRgb(fb, col, y, px) && isSilverRaw(px)) count++;
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

bool hasBottomLinePoint(const LineCounts& lc) {
    for (uint8_t i = 0; i < lc.count; i++) {
        if (lc.crossings[i].edge == LC_EDGE_BOTTOM || lc.crossings[i].pixelY >= ARC_BOTTOM_Y) return true;
    }
    return false;
}

uint8_t rawGreenOnColorRow(camera_fb_t* fb, float lineCom, uint8_t& greenLeft, uint8_t& greenRight,
                           uint8_t& blackLeft, uint8_t& blackRight) {
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
    blackLeft = 0;
    blackRight = 0;
    for (uint8_t x = leftStart; x < leftEnd; x++) {
        if (isGreen(rowPixels[x])) greenLeft++;
        else if (isBlack(rowPixels[x])) blackLeft++;
    }
    for (uint8_t x = rightStart; x < rightEnd; x++) {
        if (isGreen(rowPixels[x])) greenRight++;
        else if (isBlack(rowPixels[x])) blackRight++;
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

void clearCommit(const char* reason = "clear") {
    if (s_commitActive) xs_noteCommitEnd(reason);
    s_commitActive = false;
    s_commitSeenBranch = false;
    s_commitLocked = false;
    s_commitSettle = 0;
}

void clearCurveCommit() {
    s_curveActive = false;
    s_curveCalm = 0;
    s_curveLost = 0;
}

void startCommit(bool left) {
    s_commitActive = true;
    s_commitLeft = left;
    s_commitSeenBranch = false;
    s_commitLocked = false;
    s_commitSettle = 0;
    xs_noteCommitStart(left);
    clearCurveCommit();   // green takes over steering; drop any auto curve lock
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
            xs_noteCommitLock(s_commitLeft, lc.crossings[best].pixelX, lc.crossings[best].pixelY);
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
        if (++s_commitSettle >= COMMIT_SETTLE_FRAMES) clearCommit("settled");
    } else {
        s_commitSettle = 0;
    }
}

// The single out when there is exactly one in + one out.
int singleOut(const LineCounts& lc, int inIndex) {
    for (int i = 0; i < lc.count; i++) if (i != inIndex) return i;
    return -1;
}

// Start an auto curve commit: exactly one in + one out, the out already deep in
// the side-gain (tight-turn) zone. Pure steering lock — no flag, no green reset.
void tryStartCurveCommit(const LineCounts& lc, const LineClass& cls) {
    if (s_curveActive || s_commitActive) return;
    if (cls.inIndex < 0 || cls.outCount != 1 || lc.count != 2) return;
    const int outI = singleOut(lc, cls.inIndex);
    if (outI < 0) return;
    const Crossing& o = lc.crossings[outI];
    if (o.pixelY < ARC_SIDE_GAIN_Y) return;
    s_curveActive = true;
    s_curveLockPos = o.pos;
    s_curveCalm = 0;
    s_curveLost = 0;
}

// Sticky tracking of the committed curve out by nearest perimeter-pos (same idea
// as the green commit's locked branch), so a newly appearing crossing can't steal
// the target. Returns the matched index, or -1 on a transient miss (caller holds).
int curveCommittedOut(const LineCounts& lc, int inIndex) {
    if (!s_curveActive || inIndex < 0) return -1;
    int best = -1;
    float bestDist = 1e9f;
    for (int i = 0; i < lc.count; i++) {
        if (i == inIndex) continue;
        const float d = lc_loopDist(s_curveLockPos, lc.crossings[i].pos, lc.perimeter);
        if (d < bestDist) { best = i; bestDist = d; }
    }
    if (best >= 0 && bestDist <= COMMIT_TRACK_GATE) {
        s_curveLockPos = lc.crossings[best].pos;
        return best;
    }
    return -1;
}

// Release the curve commit once the in->out slope has calmed (line straightened)
// for CURVE_CALM_FRAMES, or after losing the committed out for CURVE_LOST_FRAMES.
void updateCurveRelease(bool fresh, float angleDeg, int steerOut) {
    if (!s_curveActive) return;
    if (steerOut < 0) {                                   // committed out unseen
        if (++s_curveLost >= CURVE_LOST_FRAMES) clearCurveCommit();
        return;
    }
    s_curveLost = 0;
    if (fresh && fabsf(angleDeg) < CURVE_CALM_ANGLE_DEG) {
        if (++s_curveCalm >= CURVE_CALM_FRAMES) clearCurveCommit();
    } else {
        s_curveCalm = 0;
    }
}

uint8_t vectorError(const Crossing& in, const Crossing& out, float sideMult,
                    float& angleOut, float& posOut) {
    const float dx = (float)out.pixelX - (float)in.pixelX;
    float dy = (float)in.pixelY - (float)out.pixelY;
    if (dy < 1.0f) dy = 1.0f;

    float angleDeg = atan2f(dx, dy) * 57.2957795f;
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

uint8_t arcAngleError(const LineCounts& lc, int inIndex, int outIndex, float& angleOut, float& posOut) {
    const Crossing& out = lc.crossings[outIndex];
    float sideMult = 1.0f;
    if ((out.edge == LC_EDGE_LEFT || out.edge == LC_EDGE_RIGHT) && out.pixelY >= ARC_SIDE_GAIN_Y) {
        sideMult = ARC_SIDE_GAIN_MULT;
    }
    return vectorError(lc.crossings[inIndex], out, sideMult, angleOut, posOut);
}

bool singleFrontRowDirection(camera_fb_t* fb, const Crossing& p, Crossing& out) {
    const int y = (int)p.pixelY + SINGLE_FRONT_ROW_DY;
    if (!fb || y < 0 || y >= fb->height) return false;

    const int x0 = max((int)COLOR_X_MIN, (int)p.pixelX - SINGLE_FRONT_ROW_HALF_W);
    const int x1 = min((int)COLOR_X_MAX, (int)p.pixelX + SINGLE_FRONT_ROW_HALF_W);
    int weighted = 0, hits = 0;
    for (int x = x0; x <= x1; x++) {
        if (!neighborhoodBlack(fb, (uint8_t)x, (uint8_t)y)) continue;
        weighted += x;
        hits++;
    }

    if (hits < SINGLE_FRONT_ROW_MIN_BLACK) return false;
    out = p;
    out.pixelX = (uint8_t)(weighted / hits);
    out.pixelY = (uint8_t)y;
    out.edge = LC_EDGE_BOTTOM;
    out.width = (uint8_t)(hits > 255 ? 255 : hits);
    return out.pixelY > p.pixelY;
}

uint8_t singlePointError(camera_fb_t* fb, const Crossing& p, float& angleOut, float& posOut) {
    Crossing q;
    if (p.pixelY < COLOR_ROW && singleFrontRowDirection(fb, p, q)) {
        return vectorError(q, p, 1.0f, angleOut, posOut);
    }

    angleOut = 0.0f;
    posOut = 0.0f;
    return LF_ERROR_CENTER;
}

}  // namespace

void modeLineFollowReset() {
    resetGreenFilter();
    clearCommit("reset");
    clearCurveCommit();
    lc_resetTracking();
    s_lastErr = LF_ERROR_CENTER;
    s_lastAngle = 0.0f;
    s_lastPos = 0.0f;
}

void modeLineFollowRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    xs_beginSensorFrame();

    LineCounts lc;
    detectArcCrossings(fb, lc);

    LineClass cls = lc_updateIn(lc);
    const int normalSteerOut = selectSteeringOut(lc, cls.inIndex);
    const uint16_t arcBlackCount = countArcBlackSamples();
    const bool arcBlackSaturated = arcBlackCount >= ARC_BLACK_CENTER_THRESHOLD;
    // digitalWrite(LED_BUILTIN, arcBlackSaturated ? LOW : HIGH);  // ESP32 LED active-LOW
    const uint8_t silverLeft = countSilverOnColumn(fb, SILVER_COL_LEFT);
    const uint8_t silverRight = countSilverOnColumn(fb, SILVER_COL_RIGHT);
    const bool silverDetected = silverLeft > SILVER_THRESHOLD || silverRight > SILVER_THRESHOLD;
    float colorCom;
    uint8_t colorBlack, redCount;
    scanColorRow(fb, colorCom, colorBlack, redCount);
    const bool bottomLinePoint = hasBottomLinePoint(lc);
    const bool gapDetected = colorBlack <= GAP_BLACK_MAX && !bottomLinePoint;
    const bool intersectionSaturated = colorBlack > INTERSECTION_BLACK_SAT_THRESHOLD;

    uint8_t greenLeft = 0, greenRight = 0, blackLeft = 0, blackRight = 0;
    uint8_t rawGreen = rawGreenOnColorRow(fb, colorCom, greenLeft, greenRight, blackLeft, blackRight);
    uint8_t greenCmd = 0;
    if (intersectionSaturated || gapDetected) {
        resetGreenFilter();
        rawGreen = 0;
    } else if (!s_commitActive) {
        greenCmd = updateGreenFilter(rawGreen);
        if (greenCmd != 0) { resetGreenFilter(); clearCurveCommit(); }
        if (greenCmd == 1) clearCommit("uturn");
        if (greenCmd == 2) startCommit(true);
        if (greenCmd == 3) startCommit(false);
    }

    uint8_t featureId = FEAT_NONE;
    uint8_t errByte = s_lastErr;
    bool fresh = false;

    // Auto curve commit: lock a tight-turn out so the mid-turn return-stub can't
    // steal the target. Suppressed during a saturated (intersection) row and
    // outranked by the green commit. Priority: green > curve > normal.
    if (arcBlackSaturated) clearCurveCommit();
    else                   tryStartCurveCommit(lc, cls);

    int steerOut;
    if (s_commitActive)     steerOut = committedOut(lc, cls.inIndex);
    else if (s_curveActive) steerOut = curveCommittedOut(lc, cls.inIndex);
    else                    steerOut = normalSteerOut;

    if (arcBlackSaturated) {
        errByte = LF_ERROR_CENTER;
        s_lastErr = errByte;
        s_lastAngle = 0.0f;
        s_lastPos = 0.0f;
    } else if (cls.inIndex >= 0 && steerOut >= 0) {
        errByte = arcAngleError(lc, cls.inIndex, steerOut, s_lastAngle, s_lastPos);
        s_lastErr = errByte;
        fresh = true;
    } else if (cls.inIndex >= 0) {
        errByte = singlePointError(fb, lc.crossings[cls.inIndex], s_lastAngle, s_lastPos);
        s_lastErr = errByte;
        fresh = true;
    } else if (lc.count == 0 || !cls.inHeld) {
        errByte = LF_ERROR_CENTER;
        s_lastErr = errByte;
        s_lastAngle = 0.0f;
        s_lastPos = 0.0f;
    }

    // Priority: silver > red > white-white gap > green.
    if (greenCmd == 1) featureId = FEAT_UTURN;
    if (gapDetected) featureId = FEAT_LINE_LOST;
    if (redCount > RED_THRESHOLD) featureId = FEAT_RED;
    if (silverDetected) featureId = FEAT_SILVER;
    updateCommitSettle(cls, lc.count);
    updateCurveRelease(fresh, s_lastAngle, steerOut);

    const bool viewerCommitActive = s_commitActive || s_curveActive;
    xs_storeLineDebug(lc, cls, steerOut, errByte, s_lastAngle);
    xs_storeLineSteer(steerOut, viewerCommitActive, s_commitLocked, s_commitSettle, greenCmd,
                      arcBlackCount, arcBlackSaturated);
    // Viewer-only sensor state. Silver and green write each side independently;
    // red, gap/no-line and plain line write both sides at their own priority.
    if (colorBlack > GAP_BLACK_MAX) {
        xs_setSensorBoth(XS_BLACK, XS_PRIO_LINE);
    }
    if (gapDetected) {
        xs_setSensorBoth(XS_WHITE, XS_PRIO_GAP);
    }
    if (greenLeft > GREEN_PX_THRESHOLD) {
        xs_setSensorSide(XS_LEFT, XS_GREEN, XS_PRIO_GREEN);
    }
    if (greenRight > GREEN_PX_THRESHOLD) {
        xs_setSensorSide(XS_RIGHT, XS_GREEN, XS_PRIO_GREEN);
    }
    if (redCount > RED_THRESHOLD) {
        xs_setSensorBoth(XS_RED, XS_PRIO_RED);
    }
    if (silverLeft > SILVER_THRESHOLD) {
        xs_setSensorSide(XS_LEFT, XS_SILVER, XS_PRIO_SILVER);
    }
    if (silverRight > SILVER_THRESHOLD) {
        xs_setSensorSide(XS_RIGHT, XS_SILVER, XS_PRIO_SILVER);
    }

    teensy.send(XIAO_REG_FEATURE, featureId);
    teensy.send(XIAO_REG_COM, errByte);
    uint8_t xiaoFlags = 0;
    if (s_commitActive) xiaoFlags |= XIAO_FLAG_COMMIT;
    if (steerOut >= 0 && lc.crossings[steerOut].pixelY >= TIGHT_SLOW_OUT_Y) {
        xiaoFlags |= XIAO_FLAG_TIGHT_SLOW;
    }
    digitalWrite(LED_BUILTIN, (featureId == FEAT_LINE_LOST) ? LOW : HIGH);  // ESP32 LED active-LOW
    teensy.send(XIAO_REG_FLAG, xiaoFlags);

    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=0 arc=1 feat=%d err=%d n=%d in=%d out=%d fresh=%d ang=%.1f pos=%.1f ccom=%.1f sL=%d sR=%d red=%d blk25=%d ablk=%d bot=%d gap=%d sat=%d asat=%d gL=%d gR=%d rawG=%d gc=%d cmt=%d cc=%d",
        featureId, errByte, lc.count, cls.inIndex, steerOut, fresh ? 1 : 0,
        s_lastAngle, s_lastPos, colorCom, silverLeft, silverRight, redCount, colorBlack, arcBlackCount, bottomLinePoint ? 1 : 0,
        gapDetected ? 1 : 0, intersectionSaturated ? 1 : 0, arcBlackSaturated ? 1 : 0, greenLeft, greenRight,
        rawGreen, greenCmd, s_commitActive ? 1 : 0, s_curveActive ? 1 : 0);
}
