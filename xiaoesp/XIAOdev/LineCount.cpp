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
        if (lc.crossings[i].edge == LC_EDGE_TOP) continue;
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

// ── Committed goal (green turn) ──────────────────────────────────────────────
static bool  s_cActive = false;
static bool  s_cLeft   = false;  // committed side: true = left branch, false = right
static int   s_cSingle = 0;      // consecutive frames with a single continuation
static bool  s_cSeen   = false;  // have we actually reached the branch (outCount >= 2) yet?
static bool  s_cLocked = false;  // acquired a committed-side branch and now sticking to it
static float s_cLockPos = 0.0f;  // last locked branch perimeter-pos (for nearest-pos tracking)
static int   s_cFrames = 0;      // loop frames since the commit started (for the arm timeout)

void lc_commitStart(bool left) {
    s_cLeft   = left;
    s_cActive = true;
    s_cSingle = 0;
    s_cSeen   = false;
    s_cLocked = false;
    s_cFrames = 0;
}

void lc_commitClear() { s_cActive = false; s_cSingle = 0; s_cSeen = false; s_cLocked = false; }
bool lc_commitActive() { return s_cActive; }
bool lc_commitLocked() { return s_cActive && s_cLocked; }
int  lc_commitProgress() { return s_cActive ? s_cSingle : 0; }

bool lc_commitUpdate(const LineCounts& lc, int inIndex, int outCount, int& outIdx) {
    outIdx = -1;
    if (!s_cActive) return false;

    // Arm timeout: a green should be immediately followed by the intersection. If
    // we never reach a branch (outCount never hit 2) within COMMIT_ARM_TIMEOUT
    // frames, the green was stray/false — cancel so the commit can't linger.
    if (!s_cSeen && ++s_cFrames >= COMMIT_ARM_TIMEOUT) {
        s_cActive = false; s_cLocked = false; return false;
    }

    // Don't end on the *approach*: a green is confirmed while the line still looks
    // like 1-in/1-out (the branch hasn't entered the ROI yet). Only after we have
    // actually seen the branch (outCount >= 2) does the single-out end-condition
    // arm. Then, once the line settles back to a single continuation for N frames
    // in a row, the intersection is behind us and the commit ends.
    if (outCount >= 2) {
        s_cSeen   = true;   // reached the intersection
        s_cSingle = 0;
    } else if (s_cSeen && outCount == COMMIT_END_OUTS) {
        if (++s_cSingle >= COMMIT_END_FRAMES) {
            s_cActive = false; s_cSingle = 0; s_cSeen = false; s_cLocked = false; return false;
        }
    } else {
        s_cSingle = 0;      // approach (not yet seen) or line lost → no progress
    }

    int best = -1;
    if (!s_cLocked) {
        // ACQUIRE: the extreme out genuinely on the committed side (past the
        // margin). Until one appears (approach / only a centred continuation)
        // there is no lock — outIdx stays -1 and steering uses the focused-out.
        for (int i = 0; i < lc.count; i++) {
            if (i == inIndex) continue;
            const float x = (float)lc.crossings[i].pixelX;
            const bool onSide = s_cLeft ? (x <= LF_CENTER_X - COMMIT_SIDE_MARGIN)
                                        : (x >= LF_CENTER_X + COMMIT_SIDE_MARGIN);
            if (!onSide) continue;
            if (best < 0 ||
                ( s_cLeft && x < (float)lc.crossings[best].pixelX) ||
                (!s_cLeft && x > (float)lc.crossings[best].pixelX)) best = i;
        }
        if (best >= 0) { s_cLocked = true; s_cLockPos = lc.crossings[best].pos; }
    } else {
        // STICKY: follow the locked branch by nearest perimeter-pos. Pos walks
        // continuously along the border as the branch rotates, so it keeps the
        // same identity (unlike pixelX, which aliases across edges). Hold
        // (outIdx = -1) on a brief miss; the commit ends via the single-out
        // streak above, not by releasing the lock.
        float bd = 1e9f;
        for (int i = 0; i < lc.count; i++) {
            if (i == inIndex) continue;
            float d = lc_loopDist(s_cLockPos, lc.crossings[i].pos, lc.perimeter);
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0 && bd <= COMMIT_TRACK_GATE) s_cLockPos = lc.crossings[best].pos;
        else best = -1;     // lost this frame → hold, stay locked
    }
    outIdx = best;
    return true;
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
            if (lc.crossings[idx].edge == LC_EDGE_TOP) {
                idx = -1;
                s_hasPrev = false;
                s_lostFrames = 0;
            } else {
                s_prevInPos  = lc.crossings[idx].pos;   // matched → advance the track
                s_lostFrames = 0;
            }
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

    const float outDx = (float)out.pixelX - LF_CENTER_X;
    const float inDx  = (float)in.pixelX  - LF_CENTER_X;

    // Pixel-space lookahead/position error, not an angle. The out/front point
    // is the main lookahead target; the in/back point keeps the near line centred.
    float errPx = (1.0f - LF_IN_BLEND) * outDx + LF_IN_BLEND * inDx;

    // If the steering goal itself is in the near side range, multiply the whole
    // blended error so the output naturally saturates toward 0/254. This keeps
    // high T-intersection branches from causing a hard turn while still making
    // near-side goals very aggressive.
    const float absOutDx = (outDx < 0.0f) ? -outDx : outDx;
    if (out.pixelY >= LF_SIDE_Y_MIN && absOutDx > LF_GOAL_SIDE_THRESHOLD) {
        errPx *= LF_GOAL_SIDE_MULT;
    }

    float e = (float)LF_ERROR_CENTER + errPx * LF_PX_SCALE;

    if (e < 0.0f)   e = 0.0f;
    if (e > 254.0f) e = 254.0f;
    if (errPxOut) *errPxOut = errPx;
    errOut = e;
    return true;
}

float lc_inOutAngleDeg(const LineCounts& lc, int inIndex, int outIndex) {
    if (inIndex < 0 || outIndex < 0) return 90.0f;   // unknown → "not straight"
    const float dx = (float)lc.crossings[outIndex].pixelX - (float)lc.crossings[inIndex].pixelX;
    float dy = (float)lc.crossings[inIndex].pixelY - (float)lc.crossings[outIndex].pixelY; // up +
    if (dy < 1.0f) dy = 1.0f;
    return fabsf(atan2f(dx, dy)) * 57.2957795f;       // 0 = vertical/straight
}

// ── Debug bridge ─────────────────────────────────────────────────────────────
static LineCounts s_dbgLc;
static LineClass  s_dbgCls;
static int        s_dbgFo  = -1;
static int        s_dbgErr = LF_ERROR_CENTER;
static float      s_dbgErrPx = 0.0f;
static bool       s_dbgValid = false;
static int        s_dbgSteer  = -1;
static bool       s_dbgCommitActive = false;
static bool       s_dbgCommitLocked = false;
static int        s_dbgCommitProgress = 0;
static uint8_t    s_dbgGreen  = 0;
static bool       s_dbgHasArc = false;
static uint8_t    s_dbgArc[10] = {};

void lc_storeSteer(int steerOut, bool commitActive, bool commitLocked, int commitProgress, uint8_t greenCmd) {
    s_dbgSteer          = steerOut;
    s_dbgCommitActive   = commitActive;
    s_dbgCommitLocked   = commitLocked;
    s_dbgCommitProgress = commitProgress;
    s_dbgGreen          = greenCmd;
}

void lc_storeDebug(const LineCounts& lc, const LineClass& cls, int focusedOut, int errByte, float errPx) {
    s_dbgLc    = lc;
    s_dbgCls   = cls;
    s_dbgFo    = focusedOut;
    s_dbgErr   = errByte;
    s_dbgErrPx = errPx;
    s_dbgValid = true;
    s_dbgHasArc = false;
}

void lc_storeArcRoi(uint8_t topX, uint8_t topY,
                    uint8_t leftX, uint8_t leftY,
                    uint8_t rightX, uint8_t rightY,
                    uint8_t bottomLeftX, uint8_t bottomLeftY,
                    uint8_t bottomRightX, uint8_t bottomRightY) {
    s_dbgArc[0] = topX;         s_dbgArc[1] = topY;
    s_dbgArc[2] = leftX;        s_dbgArc[3] = leftY;
    s_dbgArc[4] = rightX;       s_dbgArc[5] = rightY;
    s_dbgArc[6] = bottomLeftX;  s_dbgArc[7] = bottomLeftY;
    s_dbgArc[8] = bottomRightX; s_dbgArc[9] = bottomRightY;
    s_dbgHasArc = true;
}

int lc_formatDebug(char* buf, int bufLen) {
    if (!s_dbgValid || bufLen < 48) return 0;

    // Compact header — only what the viewer overlay needs, so the points after
    // "p=" are never crowded out of the line buffer.
    int o = 0;
    if (s_dbgHasArc) {
        o = snprintf(buf, bufLen,
            "[LC] arc=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d n=%d in=%d fo=%d steer=%d act=%d lock=%d prog=%d sn=%d oc=%d g=%d err=%d p=",
            s_dbgArc[0], s_dbgArc[1], s_dbgArc[2], s_dbgArc[3], s_dbgArc[4], s_dbgArc[5],
            s_dbgArc[6], s_dbgArc[7], s_dbgArc[8], s_dbgArc[9],
            s_dbgLc.count, s_dbgCls.inIndex, s_dbgFo, s_dbgSteer,
            s_dbgCommitActive ? 1 : 0, s_dbgCommitLocked ? 1 : 0, s_dbgCommitProgress,
            s_cSeen ? 1 : 0, s_dbgCls.outCount, s_dbgGreen, s_dbgErr);
    } else {
        o = snprintf(buf, bufLen,
            "[LC] n=%d in=%d fo=%d steer=%d act=%d lock=%d prog=%d sn=%d oc=%d g=%d err=%d p=",
            s_dbgLc.count, s_dbgCls.inIndex, s_dbgFo, s_dbgSteer,
            s_dbgCommitActive ? 1 : 0, s_dbgCommitLocked ? 1 : 0, s_dbgCommitProgress,
            s_cSeen ? 1 : 0, s_dbgCls.outCount, s_dbgGreen, s_dbgErr);
    }

    int nshow = s_dbgLc.count;
    if (nshow > LC_MAX_CROSSINGS) nshow = LC_MAX_CROSSINGS;
    for (int i = 0; i < nshow && o < bufLen - 16; i++) {
        char role = 'o';
        if (i == s_dbgCls.inIndex) role = 'i';
        else if (i == s_dbgSteer)  role = 'c';   // committed steering target
        else if (i == s_dbgFo)     role = 'f';
        o += snprintf(buf + o, bufLen - o, "%s%d,%d,%c",
            (i ? " " : ""),
            s_dbgLc.crossings[i].pixelX, s_dbgLc.crossings[i].pixelY,
            role);
    }
    if (o < bufLen - 1) { buf[o++] = '\n'; buf[o] = '\0'; }
    return o;
}

// ── Row-55 debug bridge ──────────────────────────────────────────────────────
static uint8_t s_row55Left  = ROW55_WHITE;
static uint8_t s_row55Right = ROW55_WHITE;
static bool    s_row55Valid = false;

static const char* row55_className(uint8_t c) {
    switch (c) {
        case ROW55_GREEN:  return "green";
        case ROW55_RED:    return "red";
        case ROW55_SILVER: return "silver";
        case ROW55_BLACK:  return "black";
        default:           return "white";
    }
}

void row55_storeDebug(uint8_t leftClass, uint8_t rightClass) {
    s_row55Left  = leftClass;
    s_row55Right = rightClass;
    s_row55Valid = true;
}

int row55_formatDebug(char* buf, int bufLen) {
    if (!s_row55Valid || bufLen < 24) return 0;
    return snprintf(buf, bufLen, "[ROW] l=%s r=%s\n",
        row55_className(s_row55Left), row55_className(s_row55Right));
}
