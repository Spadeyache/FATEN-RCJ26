#include "SilverAlign.h"
#include "Drive.h"
#include "../../config.h"
#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>

namespace Actions {
namespace SilverAlign {

namespace {
    constexpr uint8_t  SA_SEEN_FRAMES = 5;
    constexpr uint32_t SA_TIMEOUT_MS  = 5000;

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
    uint8_t seenFrames = 0;
    bool seen = false;

    while (millis() - start < SA_TIMEOUT_MS) {
        Sensors::XIAO_link::tick();
        Processing::XiaoDecode::tick(true);

        Actions::Drive::stop();
        if (Processing::XiaoDecode::silverSeen()) {
            if (++seenFrames >= SA_SEEN_FRAMES) { seen = true; break; }
        } else {
            seenFrames = 0;
        }
    }

    Actions::Drive::stop();
    Processing::XiaoDecode::setMode(XIAO_MODE_LINE);
    Processing::XiaoDecode::clearFilter();

#if PRINT_ACTIONS
    Serial.printf("SilverAlign: %s\n", seen ? "seen" : "timeout");
#endif
    return seen;
}

}  // namespace SilverAlign
}  // namespace Actions
