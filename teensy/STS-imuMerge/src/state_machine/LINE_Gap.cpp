#include "LINE_Gap.h"
#include "StateMachine.h"
#include "../../config.h"
#include "../../pins_teensy.h"

#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"
#include "../actions/Drive.h"

#include <Arduino.h>

// =============================================================================
//  LINE_Gap - gap-crossing sequence.
//
//  XIAO runs MODE_LINE_ANGLE, which sends every frame:
//    ANGLE  - slope of the line between mid and bottom scan rows (127 = 0 deg)
//    FLAG   - both-rows flag: set when both scan rows see a qualifying black chunk
//    COM    - arc crossing count (same ROI as line follow)
//
//  This is intentionally one blocking chunk instead of a mini state machine.
// =============================================================================

namespace LINE_Gap {

namespace {
    constexpr float GAP_BACK_SPEED       = -25.0f;
    constexpr uint8_t GAP_SAMPLE_FRAMES  = 5;

    constexpr float GAP_ALIGN_KP         = 1.8f;
    constexpr float GAP_ALIGN_MAX_SPEED  = 50.0f;
    constexpr float GAP_ALIGN_DEADBAND   = 5.0f;

    constexpr float GAP_FORWARD_SPEED    = 45.0f;

    inline float signedAngleDeg() {
        return Processing::XiaoDecode::gapAngle() - 127.0f;
    }

    inline void updateXiaoNow() {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);
    }

    inline void returnToLineFollow() {
        Actions::Drive::stop();
        Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
        Processing::XiaoDecode::clearFilter();
        StateMachine::transitionTo(StateMachine::LINE_FOLLOW);
    }
}

void onEnter() {
#if PRINT_STATE
    Serial.println("State: LINE_GAP");
#endif
    Actions::Drive::stop();
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE_ANGLE);
    Processing::XiaoDecode::clearFilter();
    
    // tone(BUZZER_PIN, 7000, 100);
}

void update() {
// Reverse until both scan rows see the line.
    updateXiaoNow();
    while (!Processing::XiaoDecode::gapBothRowsFlag()) {
        Actions::Drive::motor(GAP_BACK_SPEED, GAP_BACK_SPEED);
        delay(5);
        updateXiaoNow();
    }
    delay(30);
    Actions::Drive::stop();
    

// Classify Gap and line loss
    uint16_t sampleSum = 0;
    for (uint8_t i = 0; i < GAP_SAMPLE_FRAMES; i++) {
        delay(15);
        updateXiaoNow();
        sampleSum += Processing::XiaoDecode::gapLineCount();
    }
    const uint8_t avgCount = (uint8_t)((sampleSum + GAP_SAMPLE_FRAMES / 2) / GAP_SAMPLE_FRAMES);

    
#if PRINT_STATE
    Serial.print("GAP sample avg count: ");
    Serial.println(avgCount);
#endif
    
    // If there is >=2 then there is a line to follow. 0,1 mean a dead end, so we continue with gap program.
    if (avgCount >= 2) {
        returnToLineFollow();
        return;
    }

    tone(BUZZER_PIN, 7000, 100);

    // Single crossing: spin in place until the line angle is nearly straight.
    Processing::XiaoDecode::clearFilter();
    updateXiaoNow();
    while (true) {
        const float angle = signedAngleDeg();

#if PRINT_STATE
        Serial.print("GAP align angle: ");
        Serial.println(angle);
#endif

        if (angle >= -GAP_ALIGN_DEADBAND && angle <= GAP_ALIGN_DEADBAND) break;

        float power = angle * GAP_ALIGN_KP;
        if (power >  GAP_ALIGN_MAX_SPEED) power =  GAP_ALIGN_MAX_SPEED;
        if (power < -GAP_ALIGN_MAX_SPEED) power = -GAP_ALIGN_MAX_SPEED;

        // Positive angle means the line tilts right, so spin left-forward/right-back.
        Actions::Drive::motor(-power, power);
        delay(5);
        updateXiaoNow();
    }
    Actions::Drive::stop();

    // 5. Cross the gap; when both rows no longer see the same line, go back.
    updateXiaoNow();
    while (Processing::XiaoDecode::gapBothRowsFlag()) {
        Actions::Drive::motor(GAP_FORWARD_SPEED, GAP_FORWARD_SPEED);
        delay(5);
        updateXiaoNow();
    }
    delay(4000);
    returnToLineFollow();
}

}  // namespace LINE_Gap
