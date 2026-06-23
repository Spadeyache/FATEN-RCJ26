#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"

#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"

#include <Arduino.h>

// =============================================================================
//  LINE_Gap — gap-crossing sequence.
//
//  XIAO runs MODE_LINE_ANGLE, which sends every frame:
//    ANGLE  — slope of the line between mid and bottom scan rows (127 = 0°)
//    FLAG   — both-rows flag: set when both scan rows see a qualifying black chunk
//    COM    — arc crossing count (same ROI as line follow)
//
//  Sequence:
//    1. GAP_BACK_TO_FLAG  — reverse slowly until both-rows flag is set
//    2. GAP_BACK_EXTRA    — delay + reverse a fixed amount more, then stop
//    3. GAP_SAMPLE_COUNT  — collect 5 frames of crossing count; round average
//    4a. count > 1        → return to LINE_FOLLOW (robot straddles an intersection)
//    4b. count == 1       → GAP_ALIGN_ANGLE: P-spin until |angle| < 5°
//    5. GAP_CROSS_GAP     — drive forward until both-rows flag drops → LINE_FOLLOW
// =============================================================================

namespace LINE_Gap {

namespace {
    enum GapPhase : uint8_t {
        GAP_BACK_TO_FLAG,
        GAP_BACK_EXTRA,
        GAP_SAMPLE_COUNT,
        GAP_ALIGN_ANGLE,
        GAP_CROSS_GAP,
    };

    GapPhase phase = GAP_BACK_TO_FLAG;

    // Reverse speed while searching for the line (slow, hardcoded)
    constexpr float GAP_BACK_SPEED       = -25.0f;
    // Extra reverse after flag: fixed time at the same speed
    constexpr uint32_t GAP_BACK_EXTRA_MS = 120;
    // Crossing-count sample frames
    constexpr uint8_t  GAP_SAMPLE_FRAMES = 5;
    // Angle alignment: proportional gain and deadband
    constexpr float GAP_ALIGN_KP         = 0.8f;   // motor% per degree; max ±50 clamped below
    constexpr float GAP_ALIGN_MAX_SPEED  = 50.0f;
    constexpr float GAP_ALIGN_DEADBAND   = 5.0f;   // degrees — stop spinning below this
    // Gap crossing forward speed
    constexpr float GAP_FORWARD_SPEED    = 45.0f;

    uint8_t  s_sampleN   = 0;
    uint16_t s_sampleSum = 0;

    inline float signedAngleDeg() {
        return Processing::XiaoDecode::gapAngle() - 127.0f;
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_GAP");
#endif
    Actions::Drive::stop();
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE_ANGLE);
    Processing::XiaoDecode::clearFilter();
    phase    = GAP_BACK_TO_FLAG;
    s_sampleN   = 0;
    s_sampleSum = 0;
}

void update() {
    Sensors::XIAO_link::tick();
    Processing::XiaoDecode::tick(true);

    switch (phase) {

        // ── 1. Reverse until both scan rows see the line ──────────────────────
        case GAP_BACK_TO_FLAG:
            if (Processing::XiaoDecode::gapBothRowsFlag()) {
                Actions::Drive::stop();
                phase = GAP_BACK_EXTRA;
                return;
            }
            Actions::Drive::motor(GAP_BACK_SPEED, GAP_BACK_SPEED);
            return;

        // ── 2. Short extra reverse + stop ────────────────────────────────────
        case GAP_BACK_EXTRA:
            delay(30);
            Actions::Drive::motor(GAP_BACK_SPEED, GAP_BACK_SPEED);
            delay(GAP_BACK_EXTRA_MS);
            Actions::Drive::stop();
            s_sampleN   = 0;
            s_sampleSum = 0;
            phase = GAP_SAMPLE_COUNT;
            return;

        // ── 3. Sample crossing count over N frames ────────────────────────────
        case GAP_SAMPLE_COUNT: {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            s_sampleSum += Processing::XiaoDecode::gapLineCount();
            s_sampleN++;

            if (s_sampleN < GAP_SAMPLE_FRAMES) return;

            const uint8_t avgCount = (uint8_t)((s_sampleSum + GAP_SAMPLE_FRAMES / 2) / GAP_SAMPLE_FRAMES);

#if PRINT_STATE
            Serial.print("GAP sample avg count: "); Serial.println(avgCount);
#endif
            if (avgCount > 1) {
                // Multiple lines detected — straddle/intersection, return to follow
                Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
                Processing::XiaoDecode::clearFilter();
                StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
                return;
            }

            // Single line — align to its angle then cross
            Processing::XiaoDecode::clearFilter();
            phase = GAP_ALIGN_ANGLE;
            return;
        }

        // ── 4. P-spin until aligned (|angle| < deadband) ─────────────────────
        case GAP_ALIGN_ANGLE: {
            const float angle = signedAngleDeg();
#if PRINT_STATE
            Serial.print("GAP align angle: "); Serial.println(angle);
#endif
            if (angle >= -GAP_ALIGN_DEADBAND && angle <= GAP_ALIGN_DEADBAND) {
                Actions::Drive::stop();
                phase = GAP_CROSS_GAP;
                return;
            }

            float power = angle * GAP_ALIGN_KP;
            if (power >  GAP_ALIGN_MAX_SPEED) power =  GAP_ALIGN_MAX_SPEED;
            if (power < -GAP_ALIGN_MAX_SPEED) power = -GAP_ALIGN_MAX_SPEED;

            // Positive angle → line tilts right → spin left motor forward, right back
            Actions::Drive::motor(power, -power);
            return;
        }

        // ── 5. Drive forward until both-rows flag drops ───────────────────────
        case GAP_CROSS_GAP:
            if (!Processing::XiaoDecode::gapBothRowsFlag()) {
                Actions::Drive::stop();
                Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
                Processing::XiaoDecode::clearFilter();
                StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
                return;
            }
            Actions::Drive::motor(GAP_FORWARD_SPEED, GAP_FORWARD_SPEED);
            return;
    }
}

}  // namespace LINE_Gap
