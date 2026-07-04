#pragma once

#include <Arduino.h>

namespace VictimManager {

void    reset();
void    clearAll();

uint8_t count();
uint8_t liveHeld();
uint8_t deadHeld();
bool    full();

bool    acceptsType(uint8_t type);
bool    readyToDeploy();
bool    tryGrab(uint8_t type);

void    releaseLive();
void    releaseDead();

}  // namespace VictimManager
