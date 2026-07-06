#pragma once

#include <Arduino.h>

namespace DeployPlan {

void init();
void setFatenColors(uint8_t leftColor, uint8_t rightColor);
void tick();

uint8_t leftDeployCount();
uint8_t rightDeployCount();
bool    hasRemotePlan();

}  // namespace DeployPlan
