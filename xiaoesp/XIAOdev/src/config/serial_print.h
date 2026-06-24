#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Serial Print Configuration
//
//  Set a flag to 1 to enable that category of prints, 0 to silence it.
//  Tag prefixes let the PC-side viewer filter lines by category.
//
//  Tags used in the viewer filter bar:
//    [RES]  — per-frame mode result (COM, feature, angle, counts)
//    [CLS]  — per-pixel color classification (isBlack / isSilver / ...)
//    [PIX]  — raw R/G/B, H/S/V, gray for individual scan pixels
//    [SIN]  — raw bytes / packets received from Teensy
//    [SOUT] — raw bytes / packets sent to Teensy
// ─────────────────────────────────────────────────────────────────────────────

// ── Module enable flags (1 = on, 0 = off) ────────────────────────────────────
// All flags are forced off in OUTPUT_STREAM / OUTPUT_CALIBRATE.
#if defined(OUTPUT_STREAM) || defined(OUTPUT_CALIBRATE)
  #define SPRINT_RESULTS    0
  #define SPRINT_CLASSIFY   0
  #define SPRINT_RGB_HSV    0
  #define SPRINT_SERIAL_IN  0
  #define SPRINT_SERIAL_OUT 0
#else
  #define SPRINT_RESULTS    1   // [RES]  final output each frame
  #define SPRINT_CLASSIFY   0   // [CLS]  per-pixel color calls
  #define SPRINT_RGB_HSV    0   // [PIX]  raw / calibrated pixel data
  #define SPRINT_SERIAL_IN  0   // [SIN]  incoming serial from Teensy
  #define SPRINT_SERIAL_OUT 0   // [SOUT] outgoing serial to Teensy
#endif

// ── Print macros ─────────────────────────────────────────────────────────────
// SPRINT  — print a plain string value with tag
// SPRINTF — print a formatted string (snprintf into an 80-char buffer)

#define SPRINT(flag, tag, val) \
    do { if (flag) { Serial.print(tag " "); Serial.println(val); } } while(0)

#define SPRINTF(flag, tag, fmt, ...) \
    do { if (flag) { char _b[80]; snprintf(_b, sizeof(_b), tag " " fmt, ##__VA_ARGS__); Serial.println(_b); } } while(0)

