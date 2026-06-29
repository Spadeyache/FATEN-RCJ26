#include "SilverAlign.h"
#include "Drive.h"
#include "../../config.h"
#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>
#include <math.h>

namespace Actions {
namespace SilverAlign {

namespace {
    constexpr float    SA_KP            = 1.2f;   // spin %power per degree of tilt
    constexpr float    SA_MAX_SPEED     = 45.0f;  // clamp spin power
    constexpr float    SA_MIN_SPEED     = 18.0f;  // floor to overcome stiction near zero
    constexpr float    SA_DEADBAND_DEG  = 4.0f;   // |tilt| below this = aligned
    constexpr uint8_t  SA_SETTLE_FRAMES = 5;      // consecutive aligned frames to finish
    constexpr uint32_t SA_TIMEOUT_MS    = 5000;   // give up if it can't converge
    constexpr float    SA_DIR           = 1.0f;   // flip to -1.0f to mirror spin direction

    void pumpXiaoFor(uint32_t ms) {
        const uint32_t start = millis();
        while (millis() - start < ms) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick(true);
            delay(2);
        }
    }
}

bool align() {
    Processing::XiaoDecode::setMode(XIAO_MODE_EVAC_COLOR_MASK);
    pumpXiaoFor(150);
    Processing::XiaoDecode::clearFilter();

    const uint32_t start = millis();
    uint8_t settle = 0;
    bool converged = false;

    while (millis() - start < SA_TIMEOUT_MS) {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);

        // No tape in view → hold still and keep waiting (within the timeout).
        if (!Processing::XiaoDecode::silverSeen()) {
            Actions::Drive::stop();
            settle = 0;
            continue;
        }

        const float tilt = Processing::XiaoDecode::silverAlignAngle() - 127.0f;

        if (fabsf(tilt) <= SA_DEADBAND_DEG) {
            Actions::Drive::stop();
            if (++settle >= SA_SETTLE_FRAMES) { converged = true; break; }
            continue;
        }
        settle = 0;

        float power = fabsf(tilt) * SA_KP;
        if (power > SA_MAX_SPEED) power = SA_MAX_SPEED;
        if (power < SA_MIN_SPEED) power = SA_MIN_SPEED;
        const float dir = (tilt > 0.0f ? 1.0f : -1.0f) * SA_DIR;

        // In-place spin: wheels equal and opposite.
        Actions::Drive::motor(dir * power, -dir * power);
    }

    Actions::Drive::stop();
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
    Processing::XiaoDecode::clearFilter();

#if PRINT_ACTIONS
    Serial.printf("SilverAlign: %s\n", converged ? "aligned" : "timeout");
#endif
    return converged;
}

}  // namespace SilverAlign
}  // namespace Actions
