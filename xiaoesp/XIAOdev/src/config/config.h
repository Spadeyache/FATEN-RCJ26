#pragma once

#include "hardware_config.h"
#include "protocol.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Xiao ESP32-S3 — Universal Configuration
//  Edit this file to tune all vision thresholds and mode parameters.
//  Camera hardware and WiFi credentials live in their own files.
// ─────────────────────────────────────────────────────────────────────────────

// ── Output mode ──────────────────────────────────────────────────────────────
//  Define exactly ONE:
//    OUTPUT_STREAM → binary camera frames over Serial (use with HTML viewer)
//    OUTPUT_LOG    → human-readable debug text over Serial (use with serial monitor)
#define OUTPUT_STREAM
// #define OUTPUT_LOG
// #define OUTPUT_CALIBRATE// Calibration mode prints only center 10x10 raw RGB.

// OUTPUT_STREAM submode:
//   1 = send camera frames + [LC]/[ROW]/[EVT] overlay text
//   0 = send only [LC]/[ROW]/[EVT] text, no image payload
#define STREAM_SEND_CAMERA_IMAGES 1

// Debug: force XIAO to run MODE_LINE_ANGLE without Teensy/state-machine control.
// Set back to 0 before normal robot runs.
#define DEBUG_FORCE_LINE_ANGLE_MODE 0


// ── Serial baud rates ────────────────────────────────────────────────────────

// ═════════════════════════════════════════════════════════════════════════════
//  XIAO ↔ Teensy protocol  —  KEEP IN SYNC with teensy/STS-imuMerge/config.h
//  3-byte frames [255, reg, value]; value 0..254.
//
//    reg 0x01 FEATURE  X→T  per-frame event (FEAT_* below; meaning is mode-scoped)
//    reg 0x02 COM      X→T  line error 0..254 (127 = centred)
//    reg 0x03 MODE     T→X  active XIAO mode (MODE_*)
//    reg 0x04 ANGLE    X→T  gap line angle (gap mode)
//    reg 0x05 FLAG     X→T  bit0 commit, bit1 tight-turn slow drive
// ═════════════════════════════════════════════════════════════════════════════
// FEATURE byte — LINE-follow mode events (the clean contract):

// FEATURE byte — mode-scoped codes for SEARCH_LINE / NOGI modes (separate code space):

// Mode IDs received on XIAO_REG_MODE

//  Camera Vision range
#define SCAN_COL_MIN       50   // First column (inclusive)
#define SCAN_COL_MAX       110  // Last column (inclusive)

// ── Camera scan — line-follow row ────────────────────────────────────────────
#define SCAN_ROW           67   // Y row used for line-follow scan

// ── Color thresholds ─────────────────────────────────────────────────────────
// Applied to calibrated data (vision.cpp rgb888Calibration) unless noted "raw"
#define BLACK_GRAY_MAX     5   // Calibrated grayscale ≤ this → black

#define SILVER_RAW_R_MIN   250  // Raw (pre-calibration) R ≥ this
#define SILVER_RAW_G_MIN   252  // Raw G ≥ this
#define SILVER_RAW_B_MIN   252  // Raw B ≥ this

#define GREEN_HUE_MIN      80
#define GREEN_HUE_MAX      165
#define GREEN_SAT_MIN      150
#define GREEN_VAL_MIN      135

#define RED_SAT_MIN        80
#define RED_VAL_MIN        40

// ── Pixel sampling window used by updateRawGrayHSV() ─────────────────────────
// Width/height must be odd. A 5x1 sample gives horizontal smoothing without
// mixing neighboring rows.
#define VISION_SAMPLE_BOX_W 5
#define VISION_SAMPLE_BOX_H 1
#define VISION_SAMPLE_HALF_W ((VISION_SAMPLE_BOX_W - 1) / 2)
#define VISION_SAMPLE_HALF_H ((VISION_SAMPLE_BOX_H - 1) / 2)

// ── Mode 0 : Line Follow ─────────────────────────────────────────────────────
#define LF_SILVER_PixCOUNT_THRESHOLD   4    // Min silver Pixel count → report FEAT_SILVER
#define LF_RED_PixCOUNT_THRESHOLD      30   // Min red Pixel count → report FEAT_RED
#define LF_BLACK_PixCOUNT_THRESHOLD    35   // Min black pixels → report FEAT_BLACK_INTERSECT
#define LF_GREEN_PixCOUNT_THRESHOLD    5    // pixels needed to confirm green

// ── Mode 0b : Line Follow 2 (clean two-row CoM; no green/commit) ─────────────
//  Row NEAR (70): black center-of-mass → line error (0..254, 127 = centre).
//                 < LF2_NOLINE_BLACK_MIN black px on this row → no line.
//  Row FAR  (40): edge cases — red (FEAT_RED) and black saturation (→ LED on).
#define LF2_ROW_NEAR             70   // lower row used for the steering error
#define LF2_ROW_FAR              40   // front/look-ahead row used for edge cases
#define LF2_NOLINE_BLACK_MIN      5   // near-row black px below this → no line (error = centre)
#define LF2_SATURATION_BLACK_MIN 30   // far-row black px above this → saturated bar ahead (LED)
// Continuous-through-intersection handling:
#define LF2_ROW_TOP               5   // far row: does the line continue straight past the bar?
#define LF2_GREEN_ROW_A          40   // green scan row A (on the saturation line)
#define LF2_GREEN_ROW_B          55   // green scan row B (40 + 15, toward the robot)
#define LF2_STRAIGHT_BLACK_MIN    5   // row-TOP black >= this → the line continues straight
#define LF2_STRAIGHT_DEG       25.0f  // in→out angle below this ends a committed green turn
#define LF_SILVER_SIDE_COL_LEFT        25   // vertical side silver scan column
#define LF_SILVER_SIDE_COL_RIGHT      135   // 160 - 15
#define LF_SILVER_SIDE_ROW_MIN          0
#define LF_SILVER_SIDE_ROW_MAX         70
#define LF_SILVER_SIDE_THRESHOLD       12   // hits in either side column -> FEAT_SILVER

// ── Mode 1 : SearchLine ──────────────────────────────────────────────────────
// Rectangular scan region (inclusive). Frame is 160 x 120.
#define SEARCH_LINE_SCAN_X_MIN        50
#define SEARCH_LINE_SCAN_X_MAX       110
#define SEARCH_LINE_SCAN_Y_MIN        15
#define SEARCH_LINE_SCAN_Y_MAX        60
#define SEARCH_LINE_SCAN_STEP          4   // sample every Nth pixel in x and y
#define SEARCH_LINE_SILVER_THRESHOLD   5   // Min silver samples in region → FEAT_SEARCH_LINE_SILVER
#define SEARCH_LINE_BLACK_THRESHOLD    5   // Min black samples in region  → FEAT_SEARCH_LINE_BLACK

// ── Mode 2 : No-Green Intersection ──────────────────────────────────────────
// #define NOGI_SCAN_ROW_COUNT   15   // Rows scanned: y = 0 .. NOGI_SCAN_ROW_COUNT-1
#define NOGI_SCAN_ROW 15
#define NOGI_BLACK_THRESHOLD  6   // Total black pixels across all rows → detect

// ── LineCount : border-crossing detection + in/out tracking ─────────────────
// Rectangle whose border we sample (frame is 160 x 120; keep ≥2 px from edges
// because updateRawGrayHSV() samples around each point). Widen beyond the 50–110
// line-follow band so side branches are visible at the edge.
#define LC_ROI_X_MIN          40// (32 + VISION_SAMPLE_HALF_W)
#define LC_ROI_X_MAX          110// (132 - VISION_SAMPLE_HALF_W)
#define LC_ROI_Y_TOP         15   // top edge (far from robot, small pixelY)
#define LC_ROI_Y_BOT         80   // 65 bottom edge (near robot, large pixelY)

#define LC_RUN_MIN_LEN         5   // min contiguous black samples on the border to count a line (≈ line width)
#define LC_MATCH_GATE         30   // max loop-distance (samples) for the tracker to accept a match
#define LC_LOST_FRAMES         5   // frames the IN may stay unseen before the base case re-seeds
#define LF_EDGE_BLACK_THRESHOLD 5  // top/bottom ROI row black count <= this means that edge has no line

// ── Line-follow error from pixel-space lookahead/position ────────────────────
#define LF_CENTER_X           80.0f   // image column where a correctly centred line appears
#define LF_IN_BLEND            0.00f  // small stabilizing blend from the near/in point
#define LF_PX_SCALE            3.84f  // error-byte units per pixel of horizontal displacement
#define LF_ERROR_CENTER      127      // error byte that means centred
#define LF_SIDE_Y_MIN         50      // only side-boost near the robot, not high T-intersection branches
#define LF_GOAL_SIDE_THRESHOLD 18.0f  // px from centre before the goal/out point is treated as "on the side"
#define LF_GOAL_SIDE_MULT      4.0f   // multiplier for the blended error when the goal is in the side range

// ── Green command filter (rolling-window vote; mirrors Teensy CommandFilter) ─
#define GF_QUEUE_SIZE         15   // rolling window of raw green observations
#define GF_VOTES               4   // left/right votes in window → confirm a turn
#define GF_UTURN_VOTES         2   // pure both-green frames → confirm a U-turn
#define GF_BOTH_VOTES          4   // left AND right each ≥ this → also a U-turn

// ── Committed goal (green turn): virtual mid-edge point, tracked until the line
//    settles back to a single continuation (intersection passed) ─────────────
#define COMMIT_END_OUTS       1     // "single continuation" = exactly this many outs
#define COMMIT_END_FRAMES    15     // consecutive single-out *loop* frames to end (≈0.5s at camera rate,
                                    //   not viewer rate — lc_commitUpdate runs every loop)
#define COMMIT_ARM_TIMEOUT   45     // loop frames after a green to actually reach a branch (outCount>=2);
                                    //   if none appears, cancel the commit (a stray/false green fizzles)
#define COMMIT_SIDE_MARGIN   10     // px past LF_CENTER_X an out must be to ACQUIRE the lock; until a
                                    //   branch is genuinely on that side we don't lock (steering stays
                                    //   on the centred focused-out)
#define COMMIT_TRACK_GATE  50.0f    // once locked, follow that branch by nearest perimeter-pos within this
                                    //   gate (loop samples). Pos distinguishes edges/corners → holds the
                                    //   branch identity far better than pixelX as crossings converge.
#define COMMIT_SINGLE_OUT_FRAMES 6   // clear commit after seeing only one out for this many frames

