#include "vision.h"
#include "../config/config.h"
#include "../config/serial_print.h"
#include <Arduino.h>

#ifdef OUTPUT_CALIBRATE
const float R_Gain = 1.0;
const float G_Gain = 1.0;
const float B_Gain = 1.0;

const uint8_t R_D = 0;
const uint8_t G_D = 0;
const uint8_t B_D = 0;
#else
const float R_Gain = 1.98598130841;     // 1.98598130841
const float G_Gain = 2.25265017668;     // 2.25265017668
const float B_Gain = 3.1;     // 4.0

const uint8_t R_D = 12.4 * 0.8;       // avgR_Black : 0.8 is the safety margin
const uint8_t G_D = 25.2 * 0.8;       // 
const uint8_t B_D = 17.4 * 0.8;        // 
#endif

void rgb888Calibration(uint8_t& r8, uint8_t& g8, uint8_t& b8) {
    float calR = ((float)r8 - R_D) * R_Gain;
    float calG = ((float)g8 - G_D) * G_Gain;
    float calB = ((float)b8 - B_D) * B_Gain;

    // costrain to keep in 0-255 bound
    r8 = (uint8_t)constrain(calR, 0, 255);
    g8 = (uint8_t)constrain(calG, 0, 255);
    b8 = (uint8_t)constrain(calB, 0, 255);
}

HSV rgb888_to_hsv(uint8_t r8, uint8_t g8, uint8_t b8) {
    float r = r8;
    float g = g8;
    float b = b8;

    float min = fmin(fmin(r, g), b);
    float max = fmax(fmax(r, g), b);
    float delta = max - min;

    HSV res;
    res.v = (uint8_t)max; // Value

    if (max == 0 || delta == 0) {
        res.h = 0;
        res.s = 0;
        return res;
    }

    res.s = (uint8_t)((delta / max) * 255); // Saturation

    // Hue Calculation
    float h;
    if (r == max) h = (g - b) / delta;
    else if (g == max) h = 2 + (b - r) / delta;
    else h = 4 + (r - g) / delta;

    h *= 60; // Scale to 0-360 range
    if (h < 0) h += 360;

    res.h = (uint16_t)h;
    return res;
}

// Returns the averaged sample around the requested point. Current sample is
// 1 row x 5 columns: enough horizontal smoothing for binary color checks
// without mixing neighboring rows.
cameraData updateRawGrayHSV(camera_fb_t* fb, uint8_t coordX, uint8_t coordY, bool print) {

    cameraData res = {0};

    if (!fb || !fb->buf) return res;

    int w = fb->width;
    int h = fb->height;
    
    const int halfW = VISION_SAMPLE_HALF_W;
    const int halfH = VISION_SAMPLE_HALF_H;

    if (coordX < halfW) return res;
    if (coordX >= w - halfW) return res;
    if (coordY < halfH) return res;
    if (coordY >= h - halfH) return res;

    long rSum = 0, gSum = 0, bSum = 0;
    int count = 0;
    // 1. Average the configured sample box around center.
    for (int dy = -halfH; dy <= halfH; dy++) {
        for (int dx = -halfW; dx <= halfW; dx++) {
            int x = coordX + dx;
            int y = coordY + dy;
            if (x < 0 || x >= w || y < 0 || y >= h) continue;

            uint16_t px = unpackRGB565(fb->buf, y * w + x);
            uint8_t r, g, b;
            rgb565To888(px, r, g, b);

            rSum += r;
            gSum += g;
            bSum += b;
            count++;
        }
    }

    if (count == 0) return res;

    // 2. Calculate averages
    uint8_t avgR = (uint8_t)(rSum / count);
    uint8_t avgG = (uint8_t)(gSum / count);
    uint8_t avgB = (uint8_t)(bSum / count);

    // save the values to print later
    uint8_t preR = avgR;
    uint8_t preG = avgG;
    uint8_t preB = avgB;

    res.avgR = avgR; // important thats its raw values
    res.avgG = avgG;
    res.avgB = avgB;

    // apply the calibration
    rgb888Calibration(avgR, avgG, avgB);

    uint8_t gray = rgbToGray(avgR, avgG, avgB);

    // 3. Convert averaged RGB to HSV (Applying software WB inside)
    HSV hsv = rgb888_to_hsv(avgR, avgG, avgB);

    // if (print){
    //     // 4. Print unified line
    //     Serial.printf(
    //         "PRE_CALIB:[R:%3d G:%3d B:%3d] | AVG_BOX:[R:%3d G:%3d B:%3d Gray:%3d] | HSV:[H:%3d S:%3d V:%3d]\n",
    //         preR, preG, preB,
    //         avgR, avgG, avgB, gray,
    //         hsv.h, hsv.s, hsv.v
    //     );
    // }

    
    res.gray = gray;
    res.hsv = hsv;
    return res;
}

bool sampleRawRgb(camera_fb_t* fb, uint8_t coordX, uint8_t coordY, RawRgb& out) {
    out = {0, 0, 0};
    if (!fb || !fb->buf) return false;

    const int w = fb->width;
    const int h = fb->height;
    const int halfW = VISION_SAMPLE_HALF_W;
    const int halfH = VISION_SAMPLE_HALF_H;

    if (coordX < halfW) return false;
    if (coordX >= w - halfW) return false;
    if (coordY < halfH) return false;
    if (coordY >= h - halfH) return false;

    long rSum = 0, gSum = 0, bSum = 0;
    int count = 0;
    for (int dy = -halfH; dy <= halfH; dy++) {
        for (int dx = -halfW; dx <= halfW; dx++) {
            const int x = coordX + dx;
            const int y = coordY + dy;
            if (x < 0 || x >= w || y < 0 || y >= h) continue;

            uint8_t r, g, b;
            rgb565To888(unpackRGB565(fb->buf, y * w + x), r, g, b);
            rSum += r;
            gSum += g;
            bSum += b;
            count++;
        }
    }

    if (count == 0) return false;
    out.r = (uint8_t)(rSum / count);
    out.g = (uint8_t)(gSum / count);
    out.b = (uint8_t)(bSum / count);
    return true;
}

// void debugGray(camera_fb_t* fb) {
//     int w = fb->width;
//     int h = fb->height;
//     int cx = w / 2;
//     int cy = h / 2;

//     long rSum = 0, gSum = 0, bSum = 0, graySum = 0;
//     int count = 0;

//     for (int dy = -2; dy <= 2; dy++) {
//         for (int dx = -2; dx <= 2; dx++) {
//             int x = cx + dx;
//             int y = cy + dy;
//             if (x < 0 || x >= w || y < 0 || y >= h) continue;

//             size_t idx = y * w + x;
//             uint16_t px = unpackRGB565(fb->buf, idx);

//             uint8_t r8, g8, b8;
//             rgb565To888(px, r8, g8, b8);

//             uint8_t gray = rgbToGray(r8, g8, b8);

//             rSum += r8;
//             gSum += g8;
//             bSum += b8;
//             graySum += gray;
//             count++;
//         }
//     }

//     Serial.printf(
//         "r=%d g=%d b=%d gray=%d\n",
//         (int)(rSum / count),
//         (int)(gSum / count),
//         (int)(bSum / count),
//         (int)(graySum / count)
//     );
// }




// void Vision_Print(const FrameResult& result) {
//     // Serial.printf("LINE: detected=%s, offset=%.2f, darkPixels=%d, darkPercent=%.1f%%\n",
//     //               result.line.detected ? "YES" : "NO",
//     //               result.line.offset,
//     //               result.line.darkPixels,
//     //               result.line.darkPercent);

//     // Serial.printf("COLOR: R=%d, G=%d, B=%d, Green=%s, Red=%s, Blue=%s, White=%s, Black=%s\n",
//     //               result.color.r,
//     //               result.color.g,
//     //               result.color.b,
//     //               result.color.isGreen ? "YES" : "NO",
//     //               result.color.isRed ? "YES" : "NO",
//     //               result.color.isBlue ? "YES" : "NO",
//     //               result.color.isWhite ? "YES" : "NO",
//     //               result.color.isBlack ? "YES" : "NO");
// }

// ov2640 sends hibyte first
uint16_t unpackRGB565(const uint8_t* data, size_t index) {
    uint8_t highByte = data[index * 2];
    uint8_t lowByte  = data[index * 2 + 1];
    return (highByte << 8) | lowByte;
}
void rgb565To888(uint16_t px, uint8_t& r8, uint8_t& g8, uint8_t& b8) {
    uint8_t r = (px >> 11) & 0x1F;
    uint8_t g = (px >> 5)  & 0x3F;
    uint8_t b =  px        & 0x1F;

    r8 = (r << 3) | (r >> 2);
    g8 = (g << 2) | (g >> 4);
    b8 = (b << 3) | (b >> 2);
}

uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) {
    float gray = 0.299f * r + 0.587f * g + 0.114f * b;
    if (gray < 0.0f) gray = 0.0f;
    if (gray > 255.0f) gray = 255.0f;
    return (uint8_t)(gray + 0.5f);
}

// Since the camra produces a green biased image, we have the gray scale leaning more to green

void scanRow(camera_fb_t* fb, uint8_t y, uint8_t xMin, uint8_t xMax, cameraData* out) {
    for (uint8_t x = xMin; x <= xMax; x++) {
        out[x] = updateRawGrayHSV(fb, x, y, SPRINT_RESULTS);
    }
}

bool isBlack(const cameraData& d) {
    return d.gray <= BLACK_GRAY_MAX;
}

bool isSilver(const cameraData& d) {
    return (d.avgR >= SILVER_RAW_R_MIN) && (d.avgG >= SILVER_RAW_G_MIN) && (d.avgB >= SILVER_RAW_B_MIN);
}

bool isSilverRaw(const RawRgb& d) {
    return (d.r >= SILVER_RAW_R_MIN) && (d.g >= SILVER_RAW_G_MIN) && (d.b >= SILVER_RAW_B_MIN);
}

bool isGreen(const cameraData& d) {
    return (d.hsv.h >= GREEN_HUE_MIN && d.hsv.h <= GREEN_HUE_MAX)
        && (((d.hsv.s >= GREEN_SAT_MIN) && (d.hsv.v >= GREEN_VAL_MIN))
            || ((d.hsv.s >= 215) && (d.hsv.v >= 5)));
}

bool isRed(const cameraData& d) {
    return (d.hsv.h <= 20 || d.hsv.h >= 340)
        && (d.hsv.s >= RED_SAT_MIN)
        && (d.hsv.v >= RED_VAL_MIN);
}

