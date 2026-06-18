#include "LineCount.h"
#include "vision.h"
#include "config.h"

#include <math.h>

// Upper bound on perimeter samples: 2*(width+height) for a 160x120 frame.
#define LC_MAX_PERIM 600

// Per-frame border-loop scratch (single-threaded vision loop → static is fine).
static uint8_t s_black[LC_MAX_PERIM];
static uint8_t s_px[LC_MAX_PERIM];
static uint8_t s_py[LC_MAX_PERIM];
static uint8_t s_edge[LC_MAX_PERIM];

// Persistent in-point tracking state.
static bool    s_hasPrev    = false;
static float   s_prevInPos  = 0.0f;
static uint8_t s_lostFrames = 0;

// Sample one border pixel into the loop arrays at slot i.
static inline void lc_sample(camera_fb_t* fb, int x, int y, uint8_t edge, int i) {
    cameraData d = updateRawGrayHSV(fb, (uint8_t)x, (uint8_t)y);
    s_black[i] = isBlack(d) ? 1 : 0;
    s_px[i]    = (uint8_t)x;
    s_py[i]    = (uint8_t)y;
    s_edge[i]  = edge;
}

// Append a crossing (at loop sample `idx`) to the output list.
static inline void lc_registerCrossing(LineCounts& out, int idx, int len) {
    if (out.count >= LC_MAX_CROSSINGS) return;
    Crossing& c = out.crossings[out.count++];
    c.pos    = (float)idx;
    c.edge   = s_edge[idx];
    c.pixelX = s_px[idx];
    c.pixelY = s_py[idx];
    c.width  = (uint8_t)(len > 255 ? 255 : len);
}

// Turn one black run into a single crossing at the run midpoint. Keep the
// line-follow error path free of edge/side-line special cases: a line on the
// side should naturally create a large pixel-position error. If intersection
// counting later needs edge splitting, it should live in a separate scoped path.
static void lc_emitRun(LineCounts& out, int start, int runStartK, int len, int P) {
    if (len < LC_RUN_MIN_LEN) return;
    lc_registerCrossing(out, (start + runStartK + len / 2) % P, len);
}

// ── Layer 1 ──────────────────────────────────────────────────────────────────
void lc_detectCrossings(camera_fb_t* fb, LineCounts& out) {
    out.count     = 0;
    out.perimeter = 0.0f;
    if (!fb || !fb->buf) return;

    // Walk the ROI rectangle border as one closed loop, each pixel visited once:
    //   bottom (L→R) → right (B→T) → top (R→L) → left (T→B).
    int P = 0;
    for (int x = LC_ROI_X_MIN; x <= LC_ROI_X_MAX && P < LC_MAX_PERIM; x++)
        lc_sample(fb, x, LC_ROI_Y_BOT, LC_EDGE_BOTTOM, P++);
    for (int y = LC_ROI_Y_BOT - 1; y >= LC_ROI_Y_TOP && P < LC_MAX_PERIM; y--)
        lc_sample(fb, LC_ROI_X_MAX, y, LC_EDGE_RIGHT, P++);
    for (int x = LC_ROI_X_MAX - 1; x >= LC_ROI_X_MIN && P < LC_MAX_PERIM; x--)
        lc_sample(fb, x, LC_ROI_Y_TOP, LC_EDGE_TOP, P++);
    for (int y = LC_ROI_Y_TOP + 1; y <= LC_ROI_Y_BOT - 1 && P < LC_MAX_PERIM; y++)
        lc_sample(fb, LC_ROI_X_MIN, y, LC_EDGE_LEFT, P++);

    out.perimeter = (float)P;
    if (P == 0) return;

    // Find a non-black slot so no run wraps across the array boundary.
    int start = -1;
    for (int i = 0; i < P; i++) {
        if (!s_black[i]) { start = i; break; }
    }
    if (start < 0) {
        // Entire border is black → treat as a single all-encompassing crossing.
        lc_registerCrossing(out, P / 2, P);
        return;
    }

    // Linear pass over [start, start+P); runs cannot wrap because start is white.
    int len = 0, runStartK = 0;
    for (int k = 0; k < P; k++) {
        int idx = (start + k) % P;
        if (s_black[idx]) {
            if (len == 0) runStartK = k;
            len++;
        } else if (len > 0) {
            lc_emitRun(out, start, runStartK, len, P);
            len = 0;
        }
    }
    if (len > 0) lc_emitRun(out, start, runStartK, len, P);
}

// ── Layer 2 helpers ──────────────────────────────────────────────────────────
float lc_loopDist(float a, float b, float perimeter) {
    float d = fabsf(a - b);
    if (perimeter > 0.0f && d > perimeter * 0.5f) d = perimeter - d;
    return d;
}

int lc_trackPoint(float prevPos, const LineCounts& lc) {
    int   best  = -1;
    float bestD = 1e9f;
    for (int i = 0; i < lc.count; i++) {
        float d = lc_loopDist(prevPos, lc.crossings[i].pos, lc.perimeter);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) return -1;
    if (bestD > (float)LC_MATCH_GATE) return -1;   // nearest too far → line not seen
    return best;
}

int lc_lowestPixel(const LineCounts& lc) {
    int     best = -1;
    uint8_t maxY = 0;
    for (int i = 0; i < lc.count; i++) {
        if (best < 0 || lc.crossings[i].pixelY > maxY) {
            maxY = lc.crossings[i].pixelY;
            best = i;
        }
    }
    return best;
}

void lc_resetTracking() {
    s_hasPrev    = false;
    s_lostFrames = 0;
}

// ── Layer 2 ──────────────────────────────────────────────────────────────────
LineClass lc_updateIn(const LineCounts& lc) {
    LineClass r;
    r.inIndex  = -1;
    r.outCount = 0;
    r.inHeld   = false;
    r.inPos    = s_prevInPos;

    if (lc.count == 0) {
        // Nothing visible — hold the previous in-point and age the lost counter.
        r.inHeld = s_hasPrev;
        if (s_hasPrev && ++s_lostFrames >= LC_LOST_FRAMES) s_hasPrev = false;
        return r;
    }

    int idx = -1;

    if (s_hasPrev) {
        idx = lc_trackPoint(s_prevInPos, lc);
        if (idx >= 0) {
            s_prevInPos  = lc.crossings[idx].pos;   // matched → advance the track
            s_lostFrames = 0;
        } else {
            // Transient miss: hold previous pos rather than latch onto a branch.
            r.inHeld = true;
            if (++s_lostFrames >= LC_LOST_FRAMES) s_hasPrev = false;  // give up → re-seed below
        }
    }

    if (idx < 0 && !s_hasPrev) {
        // Base case: first frame or after a sustained loss → lowest pixel (nearest robot).
        idx = lc_lowestPixel(lc);
        if (idx >= 0) {
            s_prevInPos  = lc.crossings[idx].pos;
            s_hasPrev    = true;
            s_lostFrames = 0;
            r.inHeld     = false;
        }
    }

    r.inIndex  = (int8_t)idx;
    r.inPos    = (idx >= 0) ? lc.crossings[idx].pos : s_prevInPos;
    r.outCount = (idx >= 0) ? (uint8_t)(lc.count - 1) : lc.count;
    return r;
}

// ── Line-follow error ────────────────────────────────────────────────────────
int lc_focusedOut(const LineCounts& lc, int inIndex) {
    int best = -1, bestDx = 1000;
    uint8_t bestY = 0;
    for (int i = 0; i < lc.count; i++) {
        if (i == inIndex) continue;
        int dx = (int)((float)lc.crossings[i].pixelX - LF_CENTER_X);
        if (dx < 0) dx = -dx;
        // most centered; tie → higher (smaller pixelY)
        if (best < 0 || dx < bestDx ||
            (dx == bestDx && lc.crossings[i].pixelY < bestY)) {
            best = i; bestDx = dx; bestY = lc.crossings[i].pixelY;
        }
    }
    return best;
}

bool lc_slopeError(const LineCounts& lc, int inIndex, int outIndex, float& errOut, float* errPxOut) {
    if (inIndex < 0 || outIndex < 0) return false;

    const Crossing& in  = lc.crossings[inIndex];
    const Crossing& out = lc.crossings[outIndex];

    // Pixel-space lookahead/position error, not an angle. The focused out-point
    // is the main steering target; the in-point is blended in only to damp jitter.
    float errPx =
        (1.0f - LF_IN_BLEND) * ((float)out.pixelX - LF_CENTER_X)
      + LF_IN_BLEND          * ((float)in.pixelX  - LF_CENTER_X);

    float e = (float)LF_ERROR_CENTER + errPx * LF_PX_SCALE;

    if (e < 0.0f)   e = 0.0f;
    if (e > 254.0f) e = 254.0f;
    if (errPxOut) *errPxOut = errPx;
    errOut = e;
    return true;
}

// ── Debug bridge ─────────────────────────────────────────────────────────────
static LineCounts s_dbgLc;
static LineClass  s_dbgCls;
static int        s_dbgFo  = -1;
static int        s_dbgErr = LF_ERROR_CENTER;
static float      s_dbgErrPx = 0.0f;
static bool       s_dbgValid = false;

void lc_storeDebug(const LineCounts& lc, const LineClass& cls, int focusedOut, int errByte, float errPx) {
    s_dbgLc    = lc;
    s_dbgCls   = cls;
    s_dbgFo    = focusedOut;
    s_dbgErr   = errByte;
    s_dbgErrPx = errPx;
    s_dbgValid = true;
}

int lc_formatDebug(char* buf, int bufLen) {
    if (!s_dbgValid || bufLen < 48) return 0;

    const int xi = (s_dbgCls.inIndex >= 0 && s_dbgCls.inIndex < s_dbgLc.count)
        ? s_dbgLc.crossings[s_dbgCls.inIndex].pixelX : -1;
    const int xo = (s_dbgFo >= 0 && s_dbgFo < s_dbgLc.count)
        ? s_dbgLc.crossings[s_dbgFo].pixelX : -1;

    int o = snprintf(buf, bufLen,
        "[LC] box=%d,%d,%d,%d n=%d in=%d fo=%d xi=%d xo=%d epx=%.1f err=%d held=%d out=%d p=",
        LC_ROI_X_MIN, LC_ROI_Y_TOP, LC_ROI_X_MAX, LC_ROI_Y_BOT,
        s_dbgLc.count, s_dbgCls.inIndex, s_dbgFo, xi, xo, s_dbgErrPx, s_dbgErr,
        s_dbgCls.inHeld ? 1 : 0, s_dbgCls.outCount);

    int nshow = s_dbgLc.count;
    if (nshow > LC_MAX_CROSSINGS) nshow = LC_MAX_CROSSINGS;
    for (int i = 0; i < nshow && o < bufLen - 16; i++) {
        char role = 'o';
        if (i == s_dbgCls.inIndex) role = 'i';
        else if (i == s_dbgFo)     role = 'f';
        o += snprintf(buf + o, bufLen - o, "%s%d,%d,%c",
            (i ? " " : ""),
            s_dbgLc.crossings[i].pixelX, s_dbgLc.crossings[i].pixelY,
            role);
    }
    if (o < bufLen - 1) { buf[o++] = '\n'; buf[o] = '\0'; }
    return o;
}
