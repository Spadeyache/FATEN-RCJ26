#pragma once

// =============================================================================
//  Sensors::Touch — front bumper + two conductivity probes.
//
//  Pin convention: digitalRead returns HIGH(1) when OFF, LOW(0) when touching.
//  Public getters return the inverted value (true = currently active).
// =============================================================================

namespace Sensors {
namespace Touch {

void init();
void tick();

bool front();      // front bumper pressed
bool conduct0();   // conductivity probe 0
bool conduct1();   // conductivity probe 1

}  // namespace Touch
}  // namespace Sensors
