#include "Turn.h"
#include "Drive.h"
#include "../../config.h"
#include "../../pins_teensy.h"
#include "../sensors/XIAO_link.h"
#include "../processing/XiaoDecode.h"

#include <Arduino.h>
#include <math.h>

namespace Actions {
namespace Turn {

void turn(float angle_deg) {
    if (angle_deg > 0) {
        Drive::motor(TURN_RIGHT_L, TURN_RIGHT_R);
#if PRINT_ACTIONS
        Serial.println("Action: Right turn");
#endif
    } else {
        Drive::motor(TURN_LEFT_L, TURN_LEFT_R);
#if PRINT_ACTIONS
        Serial.println("Action: Left turn");
#endif
    }

    const unsigned long duration = (unsigned long)(fabsf(angle_deg) * TURN_MS_PER_DEG);
    const unsigned long start    = millis();

    // Keep the XIAO link drained during the turn so we don't backlog packets,
    // but the turn itself is blocking â€” no state-machine interaction.
    unsigned long lastComms = 0;
    while (millis() - start < duration) {
        if (millis() - lastComms >= 20) {
            Sensors::XIAO_link::tick();
            Processing::XiaoDecode::tick();
            lastComms = millis();
        }
    }

    Drive::stop();
    analogWrite(BUZZER_PIN, 0);
}

// void uTurn() {
// #if PRINT_ACTIONS
//     Serial.println("Action: U-Turn");
// #endif
//     Drive::motor(TURN_UTURN_L, TURN_UTURN_R);

//     const unsigned long start = millis();
//     while (millis() - start < (unsigned long)TURN_UTURN_MS) {
//         Sensors::XIAO_link::tick();
//         delay(5);
//     }
//     Drive::stop();
// }

}  // namespace Turn
}  // namespace Actions
