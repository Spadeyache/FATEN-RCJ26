#include "ModeLineFollow.h"
#include "../processing/vision.h"
#include "../config/config.h"
#include "../config/serial_print.h"
#include "../processing/LineCount.h"
#include "../stream/XiaoStream.h"

#include <Arduino.h>
#include <math.h>

namespace {

constexpr int ARC_MAX_SAMPLES = LF_ARC_MAX_SAMPLES;


//run this to update serial
//powershell -ExecutionPolicy Bypass -File xiaoesp\XIAOdev\src\stream\update_stream_guides.ps1

// Shift all line-follow ROI X coords right to correct optical center (lens/mirror bias).
constexpr int ARC_X_SHIFT = LF_ARC_X_SHIFT;
// Shift all line-follow ROI Y coords down (lower the boxes in the frame).
constexpr int ARC_Y_SHIFT = LF_ARC_Y_SHIFT;

// Mode 0 black-line ROI:
//   closed loop = bottom edge -> right tilted side -> top arc -> left tilted side.
//   Detection samples every point on that loop with the same black classifier.
constexpr uint8_t ARC_TOP_X = LF_ARC_TOP_X;
constexpr uint8_t ARC_TOP_Y = LF_ARC_TOP_Y;
constexpr uint8_t ARC_LEFT_X = LF_ARC_LEFT_X;
constexpr uint8_t ARC_RIGHT_X = LF_ARC_RIGHT_X;
constexpr uint8_t ARC_SIDE_Y = LF_ARC_SIDE_Y;
constexpr uint8_t ARC_BOTTOM_Y = LF_ARC_BOTTOM_Y;
constexpr uint8_t ARC_BOTTOM_LEFT_X = LF_ARC_BOTTOM_LEFT_X;
constexpr uint8_t ARC_BOTTOM_RIGHT_X = LF_ARC_BOTTOM_RIGHT_X;
constexpr float ARC_SAMPLE_SPACING = LF_ARC_SAMPLE_SPACING;
constexpr float ARC_PI = 3.14159265358979323846f;
constexpr uint8_t ARC_BOUNDARY_BAND_HALF_W = 5;  // x +/- 5 = 11 px wide
constexpr uint8_t ARC_BOUNDARY_BAND_HALF_H = 2;  // y +/- 2 = 5 px tall
constexpr uint8_t ARC_BOUNDARY_BLACK_HITS = 20; //11(5+5+1) * 5(2+2+1) =  55px can be black at max
constexpr uint8_t ARC_UPPER_SIDE_BLACK_HITS = 5;
constexpr uint8_t ARC_UPPER_SIDE_Y_MAX = 70;
constexpr uint8_t ARC_UPPER_SIDE_INNER_W = 12;
constexpr uint8_t ARC_UPPER_SIDE_HALF_H = 2;
constexpr uint8_t ARC_RUN_MIN_LEN = 14;
constexpr uint8_t ARC_CLOSE_GAP_MAX = 3;          // BB + up to 3 white + BB => one black run
constexpr uint8_t ARC_CLOSE_GAP_SIDE_BLACK = 2;
constexpr uint8_t ARC_UPPER_SIDE_CLOSE_GAP_MAX = 45;
constexpr uint8_t ARC_UPPER_SIDE_CLOSE_SIDE_BLACK = 1;

// Error = 127 + signed angle + line-center offset + a smaller in-point offset.
// The center term uses the average of in/out X so the robot body rides on the line.
constexpr float ARC_ANGLE_SCALE = 1.1f;
constexpr float ARC_CENTER_PX_SCALE = 0.45f;
constexpr float ARC_IN_PX_SCALE = 0.7f;
constexpr uint8_t ARC_SIDE_GAIN_Y = 55;  //boosts with gain in the side bellow Y : for tight turns
constexpr float ARC_SIDE_GAIN_MULT = 2.0f; //1.6
constexpr uint8_t TIGHT_SLOW_OUT_Y = 55;
constexpr uint8_t SINGLE_FRONT_ROW_DY = 12;
constexpr uint8_t SINGLE_FRONT_ROW_HALF_W = 30;
constexpr uint8_t SINGLE_FRONT_ROW_MIN_BLACK = 3;

// Silver rescue-zone tape scan: same side-column logic as the older line/search modes.
constexpr uint8_t SILVER_COL_LEFT = LF_SILVER_COL_LEFT;
constexpr uint8_t SILVER_COL_RIGHT = LF_SILVER_COL_RIGHT;
constexpr uint8_t SILVER_ROW_MIN = LF_SILVER_ROW_MIN;
constexpr uint8_t SILVER_ROW_MAX = LF_SILVER_ROW_MAX;
constexpr uint8_t SILVER_THRESHOLD = 6; //num of px

// Color processing. Bounds follow the arc ROI side walls, not the full image.
constexpr uint8_t COLOR_ROW = 45;
constexpr uint8_t COLOR_X_MIN = ARC_LEFT_X;
constexpr uint8_t COLOR_X_MAX = ARC_RIGHT_X;
constexpr uint8_t RED_ROW = 30;
constexpr uint8_t RED_SAMPLE_W = 20;
constexpr uint8_t RED_THRESHOLD = 15;
constexpr uint8_t GAP_BLACK_MAX = 5;
constexpr uint8_t GREEN_WINDOW = 37;
constexpr uint8_t GREEN_LINE_HALF_W = 2;
constexpr uint8_t GREEN_PX_THRESHOLD = 5;

// Raw green reading, sent straight to the Teensy each frame (0 none, 1 u-turn /
// both-green, 2 left, 3 right). No XIAO-side voting — the Teensy CommandFilter
// does all the debouncing (see Processing::CommandFilter).

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
constexpr uint16_t ARC_BLACK_CENTER_THRESHOLD = 75;

uint8_t s_px[ARC_MAX_SAMPLES];
uint8_t s_py[ARC_MAX_SAMPLES];
uint8_t s_edge[ARC_MAX_SAMPLES];
uint8_t s_black[ARC_MAX_SAMPLES];
int     s_sampleCount = 0;
bool    s_geometryReady = false;

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

bool sampleRawAt(camera_fb_t* fb, int x, int y, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (!fb || !fb->buf) return false;
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return false;
    rgb565To888(unpackRGB565(fb->buf, y * fb->width + x), r, g, b);
    return true;
}

bool upperTiltedSide(uint8_t edge, uint8_t y) {
    return (edge == LC_EDGE_LEFT || edge == LC_EDGE_RIGHT) && y <= ARC_UPPER_SIDE_Y_MAX;
}

bool boundarySampleBlack(camera_fb_t* fb, uint8_t x, uint8_t y, uint8_t edge) {
    uint8_t blackHits = 0;
    uint8_t saturatedHits = 0;
    const bool upperSide = upperTiltedSide(edge, y);

    const int dyMin = upperSide ? -(int)ARC_UPPER_SIDE_HALF_H : -(int)ARC_BOUNDARY_BAND_HALF_H;
    const int dyMax = upperSide ?  (int)ARC_UPPER_SIDE_HALF_H :  (int)ARC_BOUNDARY_BAND_HALF_H;
    int dxMin = -(int)ARC_BOUNDARY_BAND_HALF_W;
    int dxMax =  (int)ARC_BOUNDARY_BAND_HALF_W;
    if (upperSide && edge == LC_EDGE_LEFT) {
        dxMin = 1;
        dxMax = (int)ARC_UPPER_SIDE_INNER_W;
    } else if (upperSide && edge == LC_EDGE_RIGHT) {
        dxMin = -(int)ARC_UPPER_SIDE_INNER_W;
        dxMax = -1;
    }

    for (int dy = dyMin; dy <= dyMax; dy++) {
        for (int dx = dxMin; dx <= dxMax; dx++) {
            uint8_t r, g, b;
            if (!sampleRawAt(fb, (int)x + dx, (int)y + dy, r, g, b)) continue;

            if (r >= SILVER_RAW_R_MIN && g >= SILVER_RAW_G_MIN && b >= SILVER_RAW_B_MIN) {
                saturatedHits++;
                continue;
            }

            rgb888Calibration(r, g, b);
            if (rgbToGray(r, g, b) <= BLACK_GRAY_MAX) blackHits++;
        }
    }

    if (blackHits == 0) return false;
    const uint8_t baseThreshold = upperSide ? ARC_UPPER_SIDE_BLACK_HITS
                                            : ARC_BOUNDARY_BLACK_HITS;
    const int threshold = (int)baseThreshold - (int)saturatedHits;
    return blackHits >= threshold;
}

void addLineSamples(float x0, float y0, float x1, float y1, uint8_t edge,
                    bool includeFirst, bool includeLast) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const int steps = max(1, (int)lroundf(sqrtf(dx * dx + dy * dy) / ARC_SAMPLE_SPACING));
    const int first = includeFirst ? 0 : 1;
    const int last = includeLast ? steps : steps - 1;

    for (int i = first; i <= last; i++) {
        const float t = (float)i / (float)steps;
        addSample((int)lroundf(x0 + dx * t), (int)lroundf(y0 + dy * t), edge);
    }
}

void addArcSamples(float cx, float cy, float r, float startRad, float endRad,
                   uint8_t edge, bool includeFirst, bool includeLast) {
    float sweep = endRad - startRad;
    while (sweep > 0.0f) sweep -= 2.0f * ARC_PI;
    const int steps = max(1, (int)lroundf(fabsf(sweep) * r / ARC_SAMPLE_SPACING));
    const int first = includeFirst ? 0 : 1;
    const int last = includeLast ? steps : steps - 1;

    for (int i = first; i <= last; i++) {
        const float t = (float)i / (float)steps;
        const float a = startRad + sweep * t;
        addSample((int)lroundf(cx + cosf(a) * r), (int)lroundf(cy + sinf(a) * r), edge);
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

    const float rightA = atan2f((float)ARC_SIDE_Y - cy, (float)ARC_RIGHT_X - cx);
    const float leftA = atan2f((float)ARC_SIDE_Y - cy, (float)ARC_LEFT_X - cx);

    // Closed loop order mirrors LineCount: bottom -> right side -> top arc -> left side.
    addLineSamples(ARC_BOTTOM_LEFT_X, ARC_BOTTOM_Y, ARC_BOTTOM_RIGHT_X, ARC_BOTTOM_Y,
                   LC_EDGE_BOTTOM, true, true);
    addLineSamples(ARC_BOTTOM_RIGHT_X, ARC_BOTTOM_Y, ARC_RIGHT_X, ARC_SIDE_Y,
                   LC_EDGE_RIGHT, false, true);
    addArcSamples(cx, cy, r, rightA, leftA, LC_EDGE_TOP, false, true);
    addLineSamples(ARC_LEFT_X, ARC_SIDE_Y, ARC_BOTTOM_LEFT_X, ARC_BOTTOM_Y,
                   LC_EDGE_LEFT, false, false);

    s_geometryReady = true;
}

void sampleArcLoop(camera_fb_t* fb) {
    for (int i = 0; i < s_sampleCount; i++) {
        s_black[i] = boundarySampleBlack(fb, s_px[i], s_py[i], s_edge[i]) ? 1 : 0;
    }
}

void closeSmallArcGaps() {
    const int P = s_sampleCount;
    if (P <= 0) return;

    int start = -1;
    for (int i = 0; i < P; i++) {
        if (!s_black[i]) {
            start = i;
            break;
        }
    }
    if (start < 0) return;

    uint8_t closed[ARC_MAX_SAMPLES];
    for (int i = 0; i < P; i++) closed[i] = s_black[i];

    int k = 0;
    while (k < P) {
        const int idx = (start + k) % P;
        if (s_black[idx]) {
            k++;
            continue;
        }

        const int runStartK = k;
        int len = 0;
        while (k < P && !s_black[(start + k) % P]) {
            len++;
            k++;
        }

        if (len > ARC_CLOSE_GAP_MAX) continue;

        bool leftOk = true;
        bool rightOk = true;
        for (int j = 1; j <= ARC_CLOSE_GAP_SIDE_BLACK; j++) {
            if (!s_black[(start + runStartK - j + P) % P]) leftOk = false;
            if (!s_black[(start + runStartK + len + j - 1) % P]) rightOk = false;
        }
        if (!leftOk || !rightOk) continue;

        for (int j = 0; j < len; j++) {
            closed[(start + runStartK + j) % P] = 1;
        }
    }

    for (int i = 0; i < P; i++) s_black[i] = closed[i];
}

bool upperTiltedSideSample(int idx) {
    return upperTiltedSide(s_edge[idx], s_py[idx]);
}

void closeUpperSideArcGaps() {
    const int P = s_sampleCount;
    if (P <= 0) return;

    int start = -1;
    for (int i = 0; i < P; i++) {
        if (!s_black[i]) {
            start = i;
            break;
        }
    }
    if (start < 0) return;

    uint8_t closed[ARC_MAX_SAMPLES];
    for (int i = 0; i < P; i++) closed[i] = s_black[i];

    int k = 0;
    while (k < P) {
        const int idx = (start + k) % P;
        if (s_black[idx]) {
            k++;
            continue;
        }

        const int runStartK = k;
        const int firstGapIdx = (start + runStartK) % P;
        const uint8_t gapEdge = s_edge[firstGapIdx];
        int len = 0;
        bool allUpperSide = upperTiltedSideSample(firstGapIdx);
        while (k < P && !s_black[(start + k) % P]) {
            const int gapIdx = (start + k) % P;
            if (!upperTiltedSideSample(gapIdx) || s_edge[gapIdx] != gapEdge) allUpperSide = false;
            len++;
            k++;
        }

        if (!allUpperSide || len > ARC_UPPER_SIDE_CLOSE_GAP_MAX) continue;

        bool leftOk = true;
        bool rightOk = true;
        for (int j = 1; j <= ARC_UPPER_SIDE_CLOSE_SIDE_BLACK; j++) {
            const int leftIdx = (start + runStartK - j + P) % P;
            const int rightIdx = (start + runStartK + len + j - 1) % P;
            if (!s_black[leftIdx] || s_edge[leftIdx] != gapEdge || !upperTiltedSideSample(leftIdx)) leftOk = false;
            if (!s_black[rightIdx] || s_edge[rightIdx] != gapEdge || !upperTiltedSideSample(rightIdx)) rightOk = false;
        }
        if (!leftOk || !rightOk) continue;

        for (int j = 0; j < len; j++) {
            closed[(start + runStartK + j) % P] = 1;
        }
    }

    for (int i = 0; i < P; i++) s_black[i] = closed[i];
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

void scanColorRow(camera_fb_t* fb, float& blackCom, uint8_t& blackCount) {
    cameraData rowPixels[160] = {};
    scanRow(fb, COLOR_ROW, COLOR_X_MIN, COLOR_X_MAX, rowPixels);

    int32_t weighted = 0;
    blackCount = 0;
    for (uint8_t x = COLOR_X_MIN; x <= COLOR_X_MAX; x++) {
        if (isBlack(rowPixels[x])) { weighted += x; blackCount++; }
    }
    blackCom = blackCount ? (float)weighted / blackCount : (COLOR_X_MIN + COLOR_X_MAX) * 0.5f;
}

uint8_t countRedWindow(camera_fb_t* fb, uint8_t xStart) {
    uint8_t redCount = 0;
    for (uint8_t i = 0; i < RED_SAMPLE_W; i++) {
        const uint8_t x = xStart + i;
        if (x > COLOR_X_MAX) break;
        if (isRed(updateRawGrayHSV(fb, x, RED_ROW))) redCount++;
    }
    return redCount;
}

uint8_t scanRedRow(camera_fb_t* fb) {
    const uint8_t center = (COLOR_X_MIN + COLOR_X_MAX) / 2;
    const uint8_t half = RED_SAMPLE_W / 2;
    const uint8_t leftStart = COLOR_X_MIN;
    const uint8_t centerStart = (center > half) ? (uint8_t)(center - half) : COLOR_X_MIN;
    const uint8_t rightStart = (COLOR_X_MAX >= RED_SAMPLE_W - 1)
        ? (uint8_t)(COLOR_X_MAX - (RED_SAMPLE_W - 1))
        : COLOR_X_MIN;

    return countRedWindow(fb, leftStart)
         + countRedWindow(fb, centerStart)
         + countRedWindow(fb, rightStart);
}

bool hasBottomLinePoint(const LineCounts& lc) {
    for (uint8_t i = 0; i < lc.count; i++) {
        if (lc.crossings[i].edge == LC_EDGE_BOTTOM || lc.crossings[i].pixelY >= ARC_BOTTOM_Y) return true;
    }
    return false;
}

// bool gapByCrossings(const LineCounts& lc) {
//     if (lc.count == 0) return true;
//     return lc.count == 1 && lc.crossings[0].edge != LC_EDGE_TOP;
// }
bool gapByCrossings(const LineCounts& lc) {
    return lc.count == 0;
}

uint8_t rawGreenOnColorRow(camera_fb_t* fb, float lineCom,
                           uint8_t colorBlack, uint8_t redCount,
                           uint8_t& greenLeft, uint8_t& greenRight,
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
    uint8_t rawGreen = 0;
    if (left && right) rawGreen = 1;
    else if (left) rawGreen = 2;
    else if (right) rawGreen = 3;

    const uint8_t leftMid = (leftStart < leftEnd)
        ? (uint8_t)(((uint16_t)leftStart + (uint16_t)(leftEnd - 1)) / 2)
        : leftStart;
    const uint8_t rightMid = (rightStart < rightEnd)
        ? (uint8_t)(((uint16_t)rightStart + (uint16_t)(rightEnd - 1)) / 2)
        : rightStart;
    int16_t comSample = (int16_t)lroundf(lineCom);
    if (comSample < COLOR_X_MIN) comSample = COLOR_X_MIN;
    if (comSample > COLOR_X_MAX) comSample = COLOR_X_MAX;

    xs_storeColorRowDebug(COLOR_ROW, lineCom,
                          colorBlack, redCount,
                          greenLeft, greenRight,
                          leftStart, leftEnd,
                          rightStart, rightEnd,
                          rowPixels[leftMid].hsv.h,
                          rowPixels[leftMid].hsv.s,
                          rowPixels[leftMid].hsv.v,
                          rowPixels[rightMid].hsv.h,
                          rowPixels[rightMid].hsv.s,
                          rowPixels[rightMid].hsv.v,
                          rowPixels[comSample].hsv.h,
                          rowPixels[comSample].hsv.s,
                          rowPixels[comSample].hsv.v);
    return rawGreen;
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
    if (len < ARC_RUN_MIN_LEN) return;
    registerCrossing(out, (start + runStartK + len / 2) % perimeter, len);
}

void detectArcCrossings(camera_fb_t* fb, LineCounts& out) {
    out.count = 0;
    out.perimeter = 0.0f;
    if (!fb || !fb->buf) return;

    buildArcGeometry();
    sampleArcLoop(fb);
    closeSmallArcGaps();
    closeUpperSideArcGaps();

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

bool forwardEdge(uint8_t edge) {
    return edge == LC_EDGE_TOP || edge == LC_EDGE_LEFT || edge == LC_EDGE_RIGHT;
}

bool tightSlowOut(const Crossing& out) {
    return out.pixelY >= TIGHT_SLOW_OUT_Y;
}

void mergeTwoForwardOuts(LineCounts& lc, LineClass& cls) {
    int aIndex = -1;
    int bIndex = -1;
    uint8_t forwardOutCount = 0;
    for (int i = 0; i < lc.count; i++) {
        if (i == cls.inIndex) continue;
        if (!forwardEdge(lc.crossings[i].edge)) continue;
        if (forwardOutCount == 0) aIndex = i;
        else if (forwardOutCount == 1) bIndex = i;
        forwardOutCount++;
    }
    if (forwardOutCount != 2 || aIndex < 0 || bIndex < 0) return;

    const Crossing a = lc.crossings[aIndex];
    const Crossing b = lc.crossings[bIndex];
    if (tightSlowOut(a) || tightSlowOut(b)) return;

    float aPos = a.pos;
    float bPos = b.pos;
    if (lc.perimeter > 0.0f && fabsf(aPos - bPos) > lc.perimeter * 0.5f) {
        if (aPos < bPos) aPos += lc.perimeter;
        else             bPos += lc.perimeter;
    }

    Crossing merged;
    merged.pos = (aPos + bPos) * 0.5f;
    if (lc.perimeter > 0.0f && merged.pos >= lc.perimeter) merged.pos -= lc.perimeter;
    merged.edge = (a.edge == b.edge) ? a.edge : LC_EDGE_TOP;
    merged.pixelX = (uint8_t)(((uint16_t)a.pixelX + (uint16_t)b.pixelX) / 2);
    merged.pixelY = (uint8_t)(((uint16_t)a.pixelY + (uint16_t)b.pixelY) / 2);
    const uint16_t mergedWidth = (uint16_t)a.width + (uint16_t)b.width;
    merged.width = (uint8_t)(mergedWidth > 255 ? 255 : mergedWidth);

    lc.crossings[aIndex] = merged;
    for (int i = bIndex; i < (int)lc.count - 1; i++) {
        lc.crossings[i] = lc.crossings[i + 1];
    }
    lc.count--;
    if (cls.inIndex > bIndex) cls.inIndex--;
    cls.outCount = (cls.inIndex >= 0) ? (uint8_t)(lc.count - 1) : lc.count;
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

int selectWidestOut(const LineCounts& lc, int inIndex) {
    int best = -1;
    uint8_t bestWidth = 0;
    for (int i = 0; i < lc.count; i++) {
        if (i == inIndex) continue;
        if (best < 0 || lc.crossings[i].width > bestWidth) {
            best = i;
            bestWidth = lc.crossings[i].width;
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

// Retained for reference: green left/right are now handled as a hardcoded
// forward+turn on the Teensy (FEAT_GREEN_LEFT/RIGHT), not via this commit.
[[maybe_unused]] void startCommit(bool left) {
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

uint8_t vectorError(const Crossing& in, const Crossing& out, float sideMult, bool angleOnly,
                    float& angleOut, float& posOut) {
    const float dx = (float)out.pixelX - (float)in.pixelX;
    float dy = (float)in.pixelY - (float)out.pixelY;
    if (dy < 1.0f) dy = 1.0f;

    float angleDeg = atan2f(dx, dy) * 57.2957795f;
    const float inOffset = (float)in.pixelX - LF_CENTER_X;
    const float centerOffset = (((float)in.pixelX + (float)out.pixelX) * 0.5f) - LF_CENTER_X;
    float err = (float)LF_ERROR_CENTER + angleDeg * ARC_ANGLE_SCALE * sideMult;
    if (!angleOnly) {
        err += centerOffset * ARC_CENTER_PX_SCALE +
               inOffset * ARC_IN_PX_SCALE;
    }

    if (err < 0.0f) err = 0.0f;
    if (err > 254.0f) err = 254.0f;

    angleOut = angleDeg;
    posOut = centerOffset;
    return (uint8_t)(err + 0.5f);
}

uint8_t arcAngleError(const LineCounts& lc, int inIndex, int outIndex, float& angleOut, float& posOut) {
    const Crossing& out = lc.crossings[outIndex];
    float sideMult = 1.0f;
    if ((out.edge == LC_EDGE_LEFT || out.edge == LC_EDGE_RIGHT) && out.pixelY >= ARC_SIDE_GAIN_Y) {
        sideMult = ARC_SIDE_GAIN_MULT;
    }
    return vectorError(lc.crossings[inIndex], out, sideMult, tightSlowOut(out), angleOut, posOut);
}

bool singleFrontRowDirection(camera_fb_t* fb, const Crossing& p, Crossing& out) {
    const int y = (int)p.pixelY + SINGLE_FRONT_ROW_DY;
    if (!fb || y < 0 || y >= fb->height) return false;

    const int x0 = max((int)COLOR_X_MIN, (int)p.pixelX - SINGLE_FRONT_ROW_HALF_W);
    const int x1 = min((int)COLOR_X_MAX, (int)p.pixelX + SINGLE_FRONT_ROW_HALF_W);
    int weighted = 0, hits = 0;
    for (int x = x0; x <= x1; x++) {
        if (!boundarySampleBlack(fb, (uint8_t)x, (uint8_t)y, LC_EDGE_BOTTOM)) continue;
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
        return vectorError(q, p, 1.0f, false, angleOut, posOut);
    }

    angleOut = 0.0f;
    posOut = 0.0f;
    return LF_ERROR_CENTER;
}

}  // namespace

void modeLineFollowReset() {
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
    mergeTwoForwardOuts(lc, cls);
    const int normalSteerOut = selectSteeringOut(lc, cls.inIndex);
    const uint16_t arcBlackCount = 0; // saturation disabled
    const bool arcBlackSaturated = false;
    // const uint16_t arcBlackCount = countArcBlackSamples();
    // const bool arcBlackSaturated = arcBlackCount >= ARC_BLACK_CENTER_THRESHOLD;
    // digitalWrite(LED_BUILTIN, arcBlackSaturated ? LOW : HIGH);  // ESP32 LED active-LOW
    const uint8_t silverLeft = countSilverOnColumn(fb, SILVER_COL_LEFT);
    const uint8_t silverRight = countSilverOnColumn(fb, SILVER_COL_RIGHT);
    const bool silverDetected = silverLeft > SILVER_THRESHOLD || silverRight > SILVER_THRESHOLD;
    float colorCom;
    uint8_t colorBlack;
    scanColorRow(fb, colorCom, colorBlack);
    const uint8_t redCount = scanRedRow(fb);
    const bool bottomLinePoint = hasBottomLinePoint(lc);
    const bool gapDetected = gapByCrossings(lc);
    const bool blackIntersect = colorBlack > INTERSECTION_BLACK_SAT_THRESHOLD;
    const bool intersectionSaturated = blackIntersect;

    uint8_t greenLeft = 0, greenRight = 0, blackLeft = 0, blackRight = 0;
    uint8_t rawGreen = rawGreenOnColorRow(fb, colorCom, colorBlack, redCount,
                                          greenLeft, greenRight, blackLeft, blackRight);
    // No XIAO-side filtering: forward the raw per-frame green reading and let the
    // Teensy CommandFilter vote/debounce. (1 u-turn, 2 left, 3 right.)
    const uint8_t greenCmd = gapDetected ? 0 : rawGreen;

    uint8_t featureId = FEAT_NONE;
    uint8_t errByte = s_lastErr;
    bool fresh = false;

    // Auto curve commit: lock a tight-turn out so the mid-turn return-stub can't
    // steal the target. Suppressed during a saturated (intersection) row and
    // outranked by the green commit. Priority: green > curve > normal.
    // if (arcBlackSaturated) clearCurveCommit();
    // else                   tryStartCurveCommit(lc, cls);
    tryStartCurveCommit(lc, cls);

    int steerOut;
    if (s_commitActive)          steerOut = committedOut(lc, cls.inIndex);
    else if (s_curveActive)      steerOut = curveCommittedOut(lc, cls.inIndex);
    // else if (arcBlackSaturated)  steerOut = selectWidestOut(lc, cls.inIndex);
    else                         steerOut = normalSteerOut;

    // if (arcBlackSaturated) {
    //     errByte = LF_ERROR_CENTER;
    //     s_lastErr = errByte;
    //     s_lastAngle = 0.0f;
    //     s_lastPos = 0.0f;
    // } else
    if (cls.inIndex >= 0 && steerOut >= 0) {
        errByte = arcAngleError(lc, cls.inIndex, steerOut, s_lastAngle, s_lastPos);
        s_lastErr = errByte;
        fresh = true;
    } else if (cls.inIndex >= 0) {
        // One isolated crossing, especially near the front, is not enough to steer.
        // Wait for an in/out pair instead of line-following from a single point.
        errByte = singlePointError(fb, lc.crossings[cls.inIndex], s_lastAngle, s_lastPos);
        s_lastErr = errByte;
        fresh = true;
        // errByte = LF_ERROR_CENTER;
        // s_lastErr = errByte;
        // s_lastAngle = 0.0f;
        // s_lastPos = 0.0f;
    } else if (lc.count == 0 || !cls.inHeld) {
        errByte = LF_ERROR_CENTER;
        s_lastErr = errByte;
        s_lastAngle = 0.0f;
        s_lastPos = 0.0f;
    }

    // Priority: silver > red > white-white gap > black-intersect > green.
    if (greenCmd == 1) featureId = FEAT_UTURN;
    if (greenCmd == 2) featureId = FEAT_GREEN_LEFT;
    if (greenCmd == 3) featureId = FEAT_GREEN_RIGHT;
    if (blackIntersect && greenCmd == 0) featureId = FEAT_BLACK_INTERSECT;
    if (gapDetected) featureId = FEAT_LINE_LOST;
    if (redCount >= RED_THRESHOLD) featureId = FEAT_RED;
    if (silverDetected) featureId = FEAT_SILVER;
    updateCommitSettle(cls, lc.count);
    updateCurveRelease(fresh, s_lastAngle, steerOut);

    const bool viewerCommitActive = s_commitActive || s_curveActive;
    xs_storeLineDebug(lc, cls, steerOut, errByte, s_lastAngle);
    xs_storeLineSteer(steerOut, viewerCommitActive, s_commitLocked, s_commitSettle, greenCmd,
                      arcBlackCount, arcBlackSaturated);
    // Viewer-only sensor state. Silver and green write each side independently;
    // red, gap/no-line and plain line write both sides at their own priority.
    if (!gapDetected) {
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
    if (redCount >= RED_THRESHOLD) {
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
    if (steerOut >= 0 && tightSlowOut(lc.crossings[steerOut])) {
        xiaoFlags |= XIAO_FLAG_TIGHT_SLOW;
    }
    digitalWrite(LED_BUILTIN, HIGH);  // saturation LED disabled; ESP32 LED active-LOW
    teensy.send(XIAO_REG_FLAG, xiaoFlags);

    SPRINTF(SPRINT_RESULTS, "[RES]",
        "mode=0 arc=1 feat=%d err=%d n=%d in=%d out=%d fresh=%d ang=%.1f pos=%.1f ccom=%.1f sL=%d sR=%d red=%d blk25=%d ablk=%d bot=%d gap=%d sat=%d asat=%d gL=%d gR=%d rawG=%d gc=%d cmt=%d cc=%d tslow=%d outY=%d",
        featureId, errByte, lc.count, cls.inIndex, steerOut, fresh ? 1 : 0,
        s_lastAngle, s_lastPos, colorCom, silverLeft, silverRight, redCount, colorBlack, arcBlackCount, bottomLinePoint ? 1 : 0,
        gapDetected ? 1 : 0, intersectionSaturated ? 1 : 0, arcBlackSaturated ? 1 : 0, greenLeft, greenRight,
        rawGreen, greenCmd, s_commitActive ? 1 : 0, s_curveActive ? 1 : 0,
        (xiaoFlags & XIAO_FLAG_TIGHT_SLOW) ? 1 : 0,
        steerOut >= 0 ? (int)lc.crossings[steerOut].pixelY : -1);
}
