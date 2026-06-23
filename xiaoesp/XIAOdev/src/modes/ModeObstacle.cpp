#include "ModeObstacle.h"
#include "../processing/vision.h"
#include "../processing/LineCount.h"   // LcEdge enum (LC_EDGE_TOP/BOTTOM/LEFT/RIGHT)
#include "../config/config.h"
#include "../config/serial_print.h"
#include <math.h>

#ifndef M_PI
#define M_PI    3.14159265358979323846f
#endif
#ifndef M_PI_2
#define M_PI_2  1.57079632679489661923f
#endif

// =============================================================================
//  Mode 4 — Obstacle re-acquire config (self-contained copy; see header).
//
//  Same arc box as line follow (arc + tilted sides + bottom). Geometry copied
//  here so obstacle tuning never disturbs the line-follow path.
// =============================================================================
namespace {

// ── Box geometry (copied from ModeLineFollow) ────────────────────────────────
constexpr int     ARC_MAX_SAMPLES   = 340;
constexpr uint8_t ARC_TOP_X         = 80;
constexpr uint8_t ARC_TOP_Y         = 3;
constexpr uint8_t ARC_LEFT_X        = 25;
constexpr uint8_t ARC_RIGHT_X       = 135;
constexpr uint8_t ARC_SIDE_Y        = 33;
constexpr uint8_t ARC_BOTTOM_Y      = 85;
constexpr uint8_t ARC_BOTTOM_LEFT_X = 40;
constexpr uint8_t ARC_BOTTOM_RIGHT_X= 120;
constexpr uint8_t ARC_SAMPLE_STEP   = 1;
constexpr uint8_t BLACK_NEIGHBOR_R  = 2;

// ── Detection thresholds (copied from line follow / LineCount) ───────────────
constexpr uint8_t OBS_RUN_MIN_LEN        = 3;   // min contiguous black samples = a line (≈ line width)
constexpr uint8_t OBS_ARC_BLACK_THRESHOLD= 3;   // TOP-arc black samples needed → "see line" flag

// ── Inner-circle trace (line-continuation direction finder) ──────────────────
constexpr float   OBS_TRACE_RADIUS    = 22.0f;  // px from the chosen border point
constexpr uint8_t OBS_TRACE_STEPS     = 49;     // samples across the inner half-circle (±90°)
constexpr uint8_t OBS_TRACE_MIN_ARC   = 3;      // min contiguous black samples to accept a direction

// Box centroid — defines the "inner side" each border point traces toward.
constexpr float   BOX_CX = (float)ARC_TOP_X;                          // 80
constexpr float   BOX_CY = (float)(ARC_TOP_Y + ARC_BOTTOM_Y) * 0.5f;  // ~44

constexpr uint8_t ANGLE_CENTER = 127;

// ── Border-loop scratch (single-threaded vision loop → static is fine) ───────
uint8_t s_px[ARC_MAX_SAMPLES];
uint8_t s_py[ARC_MAX_SAMPLES];
uint8_t s_edge[ARC_MAX_SAMPLES];
uint8_t s_black[ARC_MAX_SAMPLES];
int     s_sampleCount  = 0;
bool    s_geometryReady = false;

struct ObsCrossing {
    uint8_t pixelX;
    uint8_t pixelY;
    uint8_t edge;
    uint8_t width;
};

void addSample(int x, int y, uint8_t edge) {
    if (s_sampleCount >= ARC_MAX_SAMPLES) return;
    if (x < 0) x = 0; if (x > 159) x = 159;
    if (y < 0) y = 0; if (y > 119) y = 119;
    s_px[s_sampleCount]   = (uint8_t)x;
    s_py[s_sampleCount]   = (uint8_t)y;
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

bool neighborhoodBlack(camera_fb_t* fb, int x, int y) {
    const int r = BLACK_NEIGHBOR_R;
    return isBlackPixelAt(fb, x, y) ||
           isBlackPixelAt(fb, x - r, y) || isBlackPixelAt(fb, x + r, y) ||
           isBlackPixelAt(fb, x, y - r) || isBlackPixelAt(fb, x, y + r);
}

// Build the closed arc loop once: bottom → right side → top arc → left side.
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

    for (int x = ARC_BOTTOM_LEFT_X; x <= ARC_BOTTOM_RIGHT_X; x += ARC_SAMPLE_STEP)
        addSample(x, ARC_BOTTOM_Y, LC_EDGE_BOTTOM);
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
    for (int i = 0; i < s_sampleCount; i++)
        s_black[i] = neighborhoodBlack(fb, s_px[i], s_py[i]) ? 1 : 0;
}

// Count black samples on the TOP arc only (for the see-line flag).
uint16_t countArcTopBlack() {
    uint16_t n = 0;
    for (int i = 0; i < s_sampleCount; i++)
        if (s_edge[i] == LC_EDGE_TOP && s_black[i]) n++;
    return n;
}

// Walk the loop, emit one crossing per contiguous black run (midpoint).
// Returns the count; fills `out` up to maxOut.
int detectCrossings(ObsCrossing* out, int maxOut) {
    const int P = s_sampleCount;
    if (P == 0) return 0;

    int start = -1;
    for (int i = 0; i < P; i++) if (!s_black[i]) { start = i; break; }
    if (start < 0) return 0;   // whole border black → ignore (no usable direction)

    int count = 0, len = 0, runStartK = 0;
    for (int k = 0; k <= P; k++) {
        const int idx = (start + (k % P)) % P;
        const bool black = (k < P) ? s_black[idx] : false;
        if (black) {
            if (len == 0) runStartK = k;
            len++;
        } else if (len > 0) {
            if (len >= OBS_RUN_MIN_LEN && count < maxOut) {
                const int mid = (start + runStartK + len / 2) % P;
                out[count].pixelX = s_px[mid];
                out[count].pixelY = s_py[mid];
                out[count].edge   = s_edge[mid];
                out[count].width  = (uint8_t)(len > 255 ? 255 : len);
                count++;
            }
            len = 0;
        }
    }
    return count;
}

// Choose the crossing: tilted sides (LEFT/RIGHT) outrank arc/bottom; within the
// same priority the larger chunk (width) wins. Returns index or -1.
int chooseCrossing(const ObsCrossing* c, int n) {
    int best = -1, bestPrio = -1, bestWidth = -1;
    for (int i = 0; i < n; i++) {
        const bool tilted = (c[i].edge == LC_EDGE_LEFT || c[i].edge == LC_EDGE_RIGHT);
        const int prio = tilted ? 1 : 0;            // tilted = 1 (higher), arc/bottom = 0
        if (prio > bestPrio || (prio == bestPrio && (int)c[i].width > bestWidth)) {
            best = i; bestPrio = prio; bestWidth = c[i].width;
        }
    }
    return best;
}

// Trace an inner half-circle around the chosen border point to find which way
// the line continues. Returns the signed line tilt in degrees folded to
// [-90,90] (0 = vertical), or 0 when no continuation is found.
float traceLineAngle(camera_fb_t* fb, const ObsCrossing& c) {
    const float px = (float)c.pixelX;
    const float py = (float)c.pixelY;
    // Direction toward the box interior (the "inner side").
    const float baseAng = atan2f(BOX_CY - py, BOX_CX - px);

    // Sample t over [-90°,+90°] around baseAng; mark black; find longest run.
    int bestStart = -1, bestLen = 0, runStart = -1, runLen = 0;
    for (int s = 0; s < OBS_TRACE_STEPS; s++) {
        const float t = (-M_PI_2) + (M_PI * s) / (float)(OBS_TRACE_STEPS - 1);
        const float ang = baseAng + t;
        const int qx = (int)lroundf(px + OBS_TRACE_RADIUS * cosf(ang));
        const int qy = (int)lroundf(py + OBS_TRACE_RADIUS * sinf(ang));
        if (neighborhoodBlack(fb, qx, qy)) {
            if (runLen == 0) runStart = s;
            runLen++;
            if (runLen > bestLen) { bestLen = runLen; bestStart = runStart; }
        } else {
            runLen = 0;
        }
    }

    if (bestLen < OBS_TRACE_MIN_ARC) return 0.0f;   // no traceable continuation

    // Continuation point = midpoint sample of the longest black arc.
    const int midS = bestStart + bestLen / 2;
    const float tMid = (-M_PI_2) + (M_PI * midS) / (float)(OBS_TRACE_STEPS - 1);
    const float angMid = baseAng + tMid;
    const float cx2 = px + OBS_TRACE_RADIUS * cosf(angMid);
    const float cy2 = py + OBS_TRACE_RADIUS * sinf(angMid);

    // Line direction (border point → continuation). Tilt from vertical-up.
    const float ldx = cx2 - px;
    const float ldy = py - cy2;                      // up = positive
    float deg = atan2f(ldx, ldy) * 57.2957795f;      // 0 = straight up
    // Lines are undirected → fold to [-90,90].
    if (deg >  90.0f) deg -= 180.0f;
    if (deg < -90.0f) deg += 180.0f;
    return deg;
}

}  // namespace

void modeObstacleRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    buildArcGeometry();
    sampleArcLoop(fb);

    // ── See-line flag: TOP arc only ───────────────────────────────────────────
    const uint16_t arcBlack = countArcTopBlack();
    const uint8_t seeFlag = (arcBlack >= OBS_ARC_BLACK_THRESHOLD) ? XIAO_FLAG_COMMIT : 0;

    // ── Line tilt angle from the full box ─────────────────────────────────────
    ObsCrossing crossings[16];
    const int n = detectCrossings(crossings, 16);
    float angleDeg = 0.0f;
    int chosen = -1;
    if (n > 0) {
        chosen = chooseCrossing(crossings, n);
        if (chosen >= 0) angleDeg = traceLineAngle(fb, crossings[chosen]);
    }
    const uint8_t encodedAngle = (uint8_t)constrain(
        (int)lroundf((float)ANGLE_CENTER + angleDeg), 0, 254);

    teensy.send(XIAO_REG_FLAG,  seeFlag);
    teensy.send(XIAO_REG_ANGLE, encodedAngle);

    digitalWrite(LED_BUILTIN, seeFlag ? LOW : HIGH);  // ESP32 LED active-LOW

    SPRINTF(SPRINT_RESULTS, "[OBS]",
        "mode=4 arcBlk=%d see=%d n=%d sel=%d edge=%d w=%d ang=%.1f enc=%d",
        arcBlack, seeFlag ? 1 : 0, n, chosen,
        (chosen >= 0) ? crossings[chosen].edge : -1,
        (chosen >= 0) ? crossings[chosen].width : 0,
        angleDeg, encodedAngle);
}
