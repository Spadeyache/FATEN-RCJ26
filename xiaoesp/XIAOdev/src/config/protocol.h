#pragma once

// XIAO <-> Teensy protocol.
// 3-byte frames: [255, reg, value], value is 0..254.

// Registers.
#define XIAO_REG_FEATURE   0x01  // XIAO -> Teensy: per-frame event
#define XIAO_REG_COM       0x02  // XIAO -> Teensy: line error, 127 = centered
#define XIAO_REG_MODE      0x03  // Teensy -> XIAO: active vision mode
#define XIAO_REG_ANGLE     0x04  // XIAO -> Teensy: mode-specific angle
#define XIAO_REG_FLAG      0x05  // XIAO -> Teensy: mode-specific flags

// XIAO_REG_FLAG bits.
#define XIAO_FLAG_COMMIT      0x01
#define XIAO_FLAG_TIGHT_SLOW  0x02

// FEATURE byte: line-follow mode.
#define FEAT_NONE          0
#define FEAT_UTURN         1
#define FEAT_RED           2
#define FEAT_SILVER        3
#define FEAT_LINE_LOST     4

// FEATURE byte: other modes.
#define FEAT_SEARCH_LINE_SILVER 5
#define FEAT_SEARCH_LINE_BLACK  6
#define FEAT_NOGI_INTERSECT     6

// Mode IDs received on XIAO_REG_MODE.
#define MODE_LINEFOLLOW    0
#define MODE_SEARCH_LINE   1
#define MODE_NOGI          2
#define MODE_LINE_ANGLE    3
#define MODE_OBSTACLE      4
#define MODE_SILVER_ALIGN  5
