#include "ModeEvacColorMask.h"
#include "../processing/vision.h"
#include "../config/config.h"
#include "../config/serial_print.h"
#include "../stream/XiaoStream.h"
#include <math.h>
#include <string.h>

// =============================================================================
//  Mode 5 - Evac color mask
//
//  Goal:
//    1. Detect whether a large entrance/exit tape is visible.
//    2. Classify it as silver or black without AI.
//    3. Use the configured line-follow arc ROI as the camera-vision boundary.
//    4. Report the visible tape tilt, where 0 deg means horizontal tape.
//
//  Silver:
//    Prefer silver over black because the evac entrance can contain the black
//    line running into silver tape. Silver can seed from saturated reflection or
//    from a strong connected low-chroma tape body.
//
//  Black:
//    Starts from dark pixels and grows through connected dark pixels. Small
//    debris is rejected by connected-size and geometry checks.
// =============================================================================
namespace {

constexpr uint16_t SA_FRAME_W = 160;
constexpr uint16_t SA_FRAME_H = 120;
constexpr uint16_t SA_PIXELS = SA_FRAME_W * SA_FRAME_H;
constexpr uint16_t SA_ROI_MAX_POINTS = LF_ARC_MAX_SAMPLES;
constexpr float SA_ARC_SAMPLE_SPACING = LF_ARC_SAMPLE_SPACING;
constexpr float SA_PI = 3.14159265358979323846f;

constexpr uint16_t SA_SILVER_FLASH_THRESHOLD = 40;
constexpr uint16_t SA_SILVER_BODY_ONLY_THRESHOLD = 110;
constexpr uint16_t SA_BLACK_THRESHOLD = 60;
constexpr uint16_t SA_MIN_COMPONENT_PIXELS = 40;
constexpr uint8_t  SA_MIN_LONG_AXIS_PX = 18;
constexpr uint8_t  SA_MASK_STREAM_MAX = 96;

constexpr uint8_t SA_BLACK_GRAY_MAX = 45;
constexpr uint8_t SA_SILVER_BODY_GRAY_MIN = 45;
constexpr uint8_t SA_SILVER_BODY_GRAY_MAX = 232;
constexpr uint8_t SA_SILVER_BODY_CHROMA_MAX = 38;
constexpr uint8_t SA_SILVER_GREEN_G_MIN = 70;
constexpr uint8_t SA_SILVER_GREEN_R_MAX = 80;
constexpr uint8_t SA_SILVER_GREEN_B_MAX = 95;
constexpr uint8_t SA_SILVER_GREEN_DOM_MIN = 18;

constexpr uint8_t ANGLE_CENTER = 127;

enum TapeKind : uint8_t {
    TAPE_NONE = 0,
    TAPE_SILVER = 1,
    TAPE_BLACK = 2,
};

struct TapeComponent {
    TapeKind kind = TAPE_NONE;
    uint16_t count = 0;
    uint16_t flashCount = 0;
    uint16_t blackCount = 0;
    uint8_t minX = 255;
    uint8_t maxX = 0;
    uint8_t minY = 255;
    uint8_t maxY = 0;
    float angleDeg = 0.0f;
    bool valid = false;

    float sx = 0.0f;
    float sy = 0.0f;
    float sxx = 0.0f;
    float syy = 0.0f;
    float sxy = 0.0f;

    uint8_t maskCount = 0;
    uint8_t maskX[SA_MASK_STREAM_MAX] = {};
    uint8_t maskY[SA_MASK_STREAM_MAX] = {};
};

uint8_t s_seen[SA_PIXELS];
uint16_t s_stack[SA_PIXELS];
uint8_t s_roiMask[SA_PIXELS];
uint8_t s_roiX[SA_ROI_MAX_POINTS];
uint8_t s_roiY[SA_ROI_MAX_POINTS];
uint16_t s_roiCount = 0;
bool s_roiReady = false;

inline uint16_t indexOf(uint8_t x, uint8_t y) {
    return (uint16_t)y * SA_FRAME_W + (uint16_t)x;
}

void addRoiPoint(int x, int y) {
    if (s_roiCount >= SA_ROI_MAX_POINTS) return;
    if (x < 0) x = 0;
    if (x >= (int)SA_FRAME_W) x = SA_FRAME_W - 1;
    if (y < 0) y = 0;
    if (y >= (int)SA_FRAME_H) y = SA_FRAME_H - 1;

    s_roiX[s_roiCount] = (uint8_t)x;
    s_roiY[s_roiCount] = (uint8_t)y;
    s_roiCount++;
}

void addRoiLine(float x0, float y0, float x1, float y1,
                bool includeFirst, bool includeLast) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const int steps = max(1, (int)lroundf(sqrtf(dx * dx + dy * dy) / SA_ARC_SAMPLE_SPACING));
    const int first = includeFirst ? 0 : 1;
    const int last = includeLast ? steps : steps - 1;

    for (int i = first; i <= last; i++) {
        const float t = (float)i / (float)steps;
        addRoiPoint((int)lroundf(x0 + dx * t), (int)lroundf(y0 + dy * t));
    }
}

void addRoiArc(float cx, float cy, float r, float startRad, float endRad,
               bool includeFirst, bool includeLast) {
    float sweep = endRad - startRad;
    while (sweep > 0.0f) sweep -= 2.0f * SA_PI;
    const int steps = max(1, (int)lroundf(fabsf(sweep) * r / SA_ARC_SAMPLE_SPACING));
    const int first = includeFirst ? 0 : 1;
    const int last = includeLast ? steps : steps - 1;

    for (int i = first; i <= last; i++) {
        const float t = (float)i / (float)steps;
        const float a = startRad + sweep * t;
        addRoiPoint((int)lroundf(cx + cosf(a) * r), (int)lroundf(cy + sinf(a) * r));
    }
}

bool pointInsideRoiPolygon(uint8_t x, uint8_t y) {
    bool inside = false;
    if (s_roiCount < 3) return false;

    for (uint16_t i = 0, j = s_roiCount - 1; i < s_roiCount; j = i++) {
        const float xi = (float)s_roiX[i];
        const float yi = (float)s_roiY[i];
        const float xj = (float)s_roiX[j];
        const float yj = (float)s_roiY[j];
        const bool crosses = ((yi > y) != (yj > y)) &&
            ((float)x < (xj - xi) * ((float)y - yi) / (yj - yi) + xi);
        if (crosses) inside = !inside;
    }
    return inside;
}

void buildArcRoiMask() {
    if (s_roiReady) return;

    s_roiCount = 0;
    memset(s_roiMask, 0, sizeof(s_roiMask));

    const float cx = (float)LF_ARC_TOP_X;
    const float topY = (float)LF_ARC_TOP_Y;
    const float sideX = (float)LF_ARC_RIGHT_X;
    const float sideY = (float)LF_ARC_SIDE_Y;
    const float dx = sideX - cx;
    const float cy = (sideY * sideY - topY * topY + dx * dx) / (2.0f * (sideY - topY));
    const float r = cy - topY;

    const float rightA = atan2f((float)LF_ARC_SIDE_Y - cy, (float)LF_ARC_RIGHT_X - cx);
    const float leftA = atan2f((float)LF_ARC_SIDE_Y - cy, (float)LF_ARC_LEFT_X - cx);

    addRoiLine(LF_ARC_BOTTOM_LEFT_X, LF_ARC_BOTTOM_Y,
               LF_ARC_BOTTOM_RIGHT_X, LF_ARC_BOTTOM_Y, true, true);
    addRoiLine(LF_ARC_BOTTOM_RIGHT_X, LF_ARC_BOTTOM_Y,
               LF_ARC_RIGHT_X, LF_ARC_SIDE_Y, false, true);
    addRoiArc(cx, cy, r, rightA, leftA, false, true);
    addRoiLine(LF_ARC_LEFT_X, LF_ARC_SIDE_Y,
               LF_ARC_BOTTOM_LEFT_X, LF_ARC_BOTTOM_Y, false, false);

    for (uint16_t y = 0; y < SA_FRAME_H; y++) {
        for (uint16_t x = 0; x < SA_FRAME_W; x++) {
            if (pointInsideRoiPolygon((uint8_t)x, (uint8_t)y)) {
                s_roiMask[indexOf((uint8_t)x, (uint8_t)y)] = 1;
            }
        }
    }

    s_roiReady = true;
}

bool insideArcRoi(uint8_t x, uint8_t y) {
    buildArcRoiMask();
    return s_roiMask[indexOf(x, y)] != 0;
}

inline uint8_t rawGray(const RawRgb& px) {
    return rgbToGray(px.r, px.g, px.b);
}

inline uint8_t rawChroma(const RawRgb& px) {
    const uint8_t mn = min(px.r, min(px.g, px.b));
    const uint8_t mx = max(px.r, max(px.g, px.b));
    return mx - mn;
}

bool rawBlack(const RawRgb& px) {
    uint8_t r = px.r;
    uint8_t g = px.g;
    uint8_t b = px.b;
    rgb888Calibration(r, g, b);
    return rgbToGray(r, g, b) <= SA_BLACK_GRAY_MAX;
}

bool silverBody(const RawRgb& px) {
    const uint8_t gray = rawGray(px);
    const bool graySilver =
        gray >= SA_SILVER_BODY_GRAY_MIN &&
        gray <= SA_SILVER_BODY_GRAY_MAX &&
        rawChroma(px) <= SA_SILVER_BODY_CHROMA_MAX;

    // The evac silver tape can look green under the XIAO lamp/camera. Treat a
    // bright green-cast, non-black surface as silver body so the black line
    // entering the silver tape does not steal the mask.
    const bool greenCastSilver =
        px.g >= SA_SILVER_GREEN_G_MIN &&
        px.r <= SA_SILVER_GREEN_R_MAX &&
        px.b <= SA_SILVER_GREEN_B_MAX &&
        px.g >= px.r + SA_SILVER_GREEN_DOM_MIN &&
        px.g >= px.b + SA_SILVER_GREEN_DOM_MIN;

    return graySilver || greenCastSilver;
}

bool pixelMatches(camera_fb_t* fb, uint8_t x, uint8_t y, TapeKind kind,
                  bool& isFlash, bool& isBlackCore) {
    isFlash = false;
    isBlackCore = false;

    if (!insideArcRoi(x, y)) return false;

    RawRgb px;
    if (!sampleRawRgb(fb, x, y, px)) return false;

    isFlash = isSilverRaw(px);
    isBlackCore = rawBlack(px);

    if (kind == TAPE_SILVER) {
        return isFlash || silverBody(px);
    }
    if (kind == TAPE_BLACK) {
        return isBlackCore;
    }
    return false;
}

void addPoint(TapeComponent& c, uint8_t x, uint8_t y,
              bool isFlash, bool isBlackCore) {
    c.count++;
    if (isFlash) c.flashCount++;
    if (isBlackCore) c.blackCount++;

    if (x < c.minX) c.minX = x;
    if (x > c.maxX) c.maxX = x;
    if (y < c.minY) c.minY = y;
    if (y > c.maxY) c.maxY = y;

    const float xf = (float)x;
    const float yf = (float)y;
    c.sx += xf;
    c.sy += yf;
    c.sxx += xf * xf;
    c.syy += yf * yf;
    c.sxy += xf * yf;

    if ((c.count & 0x03) == 0 && c.maskCount < SA_MASK_STREAM_MAX) {
        c.maskX[c.maskCount] = x;
        c.maskY[c.maskCount] = y;
        c.maskCount++;
    }
}

void finishComponent(TapeComponent& c) {
    if (c.count < 2) return;

    const float n = (float)c.count;
    const float meanX = c.sx / n;
    const float meanY = c.sy / n;
    const float covXX = c.sxx / n - meanX * meanX;
    const float covYY = c.syy / n - meanY * meanY;
    const float covXY = c.sxy / n - meanX * meanY;

    // Principal axis of the connected mask. 0 deg means horizontal tape.
    float angle = 0.5f * atan2f(2.0f * covXY, covXX - covYY) * 57.2957795f;
    while (angle > 90.0f) angle -= 180.0f;
    while (angle < -90.0f) angle += 180.0f;
    c.angleDeg = angle;

    const uint8_t w = c.maxX >= c.minX ? (uint8_t)(c.maxX - c.minX + 1) : 0;
    const uint8_t h = c.maxY >= c.minY ? (uint8_t)(c.maxY - c.minY + 1) : 0;
    const uint8_t longAxis = max(w, h);

    const bool enoughShape = c.count >= SA_MIN_COMPONENT_PIXELS &&
                             longAxis >= SA_MIN_LONG_AXIS_PX;
    const bool enoughColor =
        (c.kind == TAPE_SILVER &&
            (c.flashCount >= SA_SILVER_FLASH_THRESHOLD ||
             c.count >= SA_SILVER_BODY_ONLY_THRESHOLD)) ||
        (c.kind == TAPE_BLACK && c.blackCount >= SA_BLACK_THRESHOLD);

    c.valid = enoughShape && enoughColor;
}

TapeComponent floodFrom(camera_fb_t* fb, uint8_t seedX, uint8_t seedY,
                        TapeKind kind, uint8_t mark) {
    TapeComponent c;
    c.kind = kind;

    uint16_t sp = 0;
    const uint16_t seedIdx = indexOf(seedX, seedY);
    s_seen[seedIdx] = mark;
    s_stack[sp++] = seedIdx;

    while (sp > 0) {
        const uint16_t idx = s_stack[--sp];
        const uint8_t x = (uint8_t)(idx % SA_FRAME_W);
        const uint8_t y = (uint8_t)(idx / SA_FRAME_W);

        bool isFlash;
        bool isBlackCore;
        if (!pixelMatches(fb, x, y, kind, isFlash, isBlackCore)) continue;
        addPoint(c, x, y, isFlash, isBlackCore);

        const int nx[4] = { (int)x + 1, (int)x - 1, (int)x,     (int)x     };
        const int ny[4] = { (int)y,     (int)y,     (int)y + 1, (int)y - 1 };
        for (uint8_t i = 0; i < 4; i++) {
            if (nx[i] < 0 || nx[i] >= (int)SA_FRAME_W ||
                ny[i] < 0 || ny[i] >= (int)SA_FRAME_H ||
                !insideArcRoi((uint8_t)nx[i], (uint8_t)ny[i])) {
                continue;
            }

            const uint16_t nIdx = indexOf((uint8_t)nx[i], (uint8_t)ny[i]);
            if (s_seen[nIdx] == mark) continue;
            s_seen[nIdx] = mark;

            bool nFlash;
            bool nBlackCore;
            if (!pixelMatches(fb, (uint8_t)nx[i], (uint8_t)ny[i],
                              kind, nFlash, nBlackCore)) {
                continue;
            }
            if (sp < SA_PIXELS) s_stack[sp++] = nIdx;
        }
    }

    finishComponent(c);
    return c;
}

bool betterComponent(const TapeComponent& a, const TapeComponent& b) {
    if (!a.valid) return false;
    if (!b.valid) return true;
    if (a.kind == TAPE_SILVER && b.kind == TAPE_BLACK) return true;
    if (a.kind == TAPE_BLACK && b.kind == TAPE_SILVER) return false;
    return a.count > b.count;
}

bool seedMatchesKind(TapeKind kind, bool isFlash, bool isBlackCore) {
    if (kind == TAPE_SILVER) return true;
    if (kind == TAPE_BLACK) return isBlackCore;
    (void)isFlash;
    return false;
}

TapeComponent detectKind(camera_fb_t* fb, TapeKind kind, uint8_t mark) {
    TapeComponent best;

    for (uint16_t y = 0; y < SA_FRAME_H; y++) {
        for (uint16_t x = 0; x < SA_FRAME_W; x++) {
            const uint8_t ux = (uint8_t)x;
            const uint8_t uy = (uint8_t)y;
            if (!insideArcRoi(ux, uy)) continue;

            const uint16_t idx = indexOf(ux, uy);
            if (s_seen[idx] == mark) continue;

            bool isFlash;
            bool isBlackCore;
            if (!pixelMatches(fb, ux, uy, kind, isFlash, isBlackCore)) continue;

            if (!seedMatchesKind(kind, isFlash, isBlackCore)) continue;

            TapeComponent c = floodFrom(fb, ux, uy, kind, mark);
            if (c.valid) return c;
            if (betterComponent(c, best)) best = c;
        }
    }

    return best;
}

}  // namespace

void modeEvacColorMaskRun(camera_fb_t* fb, YacheEncodedSerial& teensy) {
    memset(s_seen, 0, sizeof(s_seen));

    const TapeComponent silver = detectKind(fb, TAPE_SILVER, 1);
    const TapeComponent black = detectKind(fb, TAPE_BLACK, 2);

    TapeComponent tape;
    if (silver.valid) tape = silver;
    else if (black.valid) tape = black;

    const bool seen = tape.valid;
    const bool isSilver = seen && tape.kind == TAPE_SILVER;
    const bool isBlack = seen && tape.kind == TAPE_BLACK;
    const char* clsName = isSilver ? "silver" : (isBlack ? "black" : "none");

    const uint8_t encodedAngle = (uint8_t)constrain(
        (int)lroundf((float)ANGLE_CENTER + (seen ? tape.angleDeg : 0.0f)), 0, 254);
    const uint8_t countByte = (uint8_t)constrain((int)tape.count, 0, 254);

    uint8_t flags = 0;
    if (seen) flags |= XIAO_FLAG_COMMIT;
    if (isSilver) flags |= XIAO_FLAG_TIGHT_SLOW;
    if (isBlack) flags |= XIAO_FLAG_BOTTOM_LINE;

    teensy.send(XIAO_REG_ANGLE, encodedAngle);
    teensy.send(XIAO_REG_FLAG, flags);
    teensy.send(XIAO_REG_COM, countByte);

    xs_storeEvacTapeDebug(
        seen,
        clsName,
        seen ? tape.angleDeg : 0.0f,
        encodedAngle,
        tape.count,
        tape.flashCount,
        tape.blackCount,
        seen ? tape.minX : 0,
        seen ? tape.minY : 0,
        seen ? tape.maxX : 0,
        seen ? tape.maxY : 0,
        seen ? tape.maskX : nullptr,
        seen ? tape.maskY : nullptr,
        seen ? tape.maskCount : 0);

    digitalWrite(LED_BUILTIN, seen ? LOW : HIGH);  // ESP32 LED active-LOW

    SPRINTF(SPRINT_RESULTS, "[SA]",
        "mode=5 seen=%d cls=%s angle=%.1f enc=%d cnt=%d flash=%d black=%d box=%d,%d,%d,%d silverCnt=%d silverFlash=%d silverOk=%d silverAngle=%.1f blackCnt=%d blackCore=%d blackOk=%d blackAngle=%.1f",
        seen ? 1 : 0,
        clsName,
        seen ? tape.angleDeg : 0.0f,
        encodedAngle,
        tape.count,
        tape.flashCount,
        tape.blackCount,
        seen ? tape.minX : 0,
        seen ? tape.minY : 0,
        seen ? tape.maxX : 0,
        seen ? tape.maxY : 0,
        silver.count,
        silver.flashCount,
        silver.valid ? 1 : 0,
        silver.angleDeg,
        black.count,
        black.blackCount,
        black.valid ? 1 : 0,
        black.angleDeg);
}
