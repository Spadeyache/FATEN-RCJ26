#pragma once

#include <Arduino.h>
#include "../drivers/yacheEncodedSerial.h"

namespace FatenCoop {

void begin();
void tick(YacheEncodedSerial& teensy);

}  // namespace FatenCoop
