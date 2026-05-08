#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Xiao ESP32-S3 — Universal Configuration
//  Edit this file to tune all vision thresholds and mode parameters.
//  Camera hardware and WiFi credentials live in their own files.
// ─────────────────────────────────────────────────────────────────────────────

// ── Serial baud rates ────────────────────────────────────────────────────────
#define SERIAL_DEBUG_BAUD    115200
#define SERIAL_TEENSY_BAUD   4000000

// ── Serial register IDs (shared protocol with Teensy) ───────────────────────
#define XIAO_REG_FEATURE   0x01   // Xiao → Teensy : detected feature
#define XIAO_REG_COM       0x02   // Xiao → Teensy : line center-of-mass
#define XIAO_REG_MODE      0x03   // Teensy → Xiao : active mode
#define XIAO_REG_ANGLE     0x04   // Xiao → Teensy : gap line angle (mode 3)

// Feature IDs sent on XIAO_REG_FEATURE
#define FEAT_NONE          0
#define FEAT_UTURN         1
#define FEAT_GREEN_LEFT    2
#define FEAT_GREEN_RIGHT   3
#define FEAT_RED           4
#define FEAT_SILVER        5   // line-follow silver
#define FEAT_BLACK_INTERSECT 6
#define FEAT_EVAC_SILVER   5   // evac-mode silver tape
#define FEAT_EVAC_BLACK    6   // evac-mode black return line
#define FEAT_NOGI_INTERSECT 6  // no-green intersection detected

// Mode IDs received on XIAO_REG_MODE
#define MODE_LINEFOLLOW    0
#define MODE_EVAC          1
#define MODE_NOGI          2
#define MODE_GAP           3
// evac position correct mode

//  Camera Vision range
#define SCAN_COL_MIN       50   // First column (inclusive)
#define SCAN_COL_MAX       110  // Last column (inclusive)

// ── Camera scan — line-follow row ────────────────────────────────────────────
#define SCAN_ROW           55   // Y row used for line-follow scan

// ── Color thresholds ─────────────────────────────────────────────────────────
// Applied to calibrated data (vision.cpp rgb888Calibration) unless noted "raw"
#define BLACK_GRAY_MAX     15   // Calibrated grayscale ≤ this → black

#define SILVER_RAW_R_MIN   250  // Raw (pre-calibration) R ≥ this
#define SILVER_RAW_G_MIN   252  // Raw G ≥ this
#define SILVER_RAW_B_MIN   252  // Raw B ≥ this

#define GREEN_HUE_MIN      80
#define GREEN_HUE_MAX      165
#define GREEN_SAT_MIN      150
#define GREEN_VAL_MIN      135

#define RED_SAT_MIN        80
#define RED_VAL_MIN        40

// ── Mode 0 : Line Follow ─────────────────────────────────────────────────────
#define LF_SILVER_PixCOUNT_THRESHOLD   9    // Min silver Pixel count → report FEAT_SILVER
#define LF_RED_PixCOUNT_THRESHOLD      30   // Min red Pixel count → report FEAT_RED
#define LF_BLACK_PixCOUNT_THRESHOLD    30   // Min black pixels → report FEAT_BLACK_INTERSECT
#define LF_GREEN_PixCOUNT_THRESHOLD    5    // pixels needed to confirm green

// ── Mode 1 : Evac ────────────────────────────────────────────────────────────
#define EVAC_SCAN_ROW         15   // Y row (near top = further ahead of robot)
#define EVAC_SILVER_THRESHOLD  5   // Min silver pixels → report FEAT_EVAC_SILVER
#define EVAC_BLACK_THRESHOLD   5   // Min black pixels → report FEAT_EVAC_BLACK

// ── Mode 2 : No-Green Intersection ──────────────────────────────────────────
// #define NOGI_SCAN_ROW_COUNT   15   // Rows scanned: y = 0 .. NOGI_SCAN_ROW_COUNT-1
#define NOGI_SCAN_ROW 15
#define NOGI_BLACK_THRESHOLD  10   // Total black pixels across all rows → detect

// ── Mode 3 : Gap (line-angle estimation) ────────────────────────────────────
#define GAP_SCAN_ROW_START    45   // Top row of multi-row scan window
#define GAP_SCAN_ROW_COUNT    12   // Number of rows in window
#define GAP_ANGLE_CENTER     127   // Encoded value for 0°
#define GAP_ANGLE_SCALE     1.41f  // Degrees-to-counts: ±90° maps to ±127 counts
