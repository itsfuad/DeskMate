#pragma once

#include <Arduino.h>

enum class InternetState : uint8_t {
  Unknown,
  Offline,
  Online,
};

void connectivityBegin(bool probeEnabled);
void connectivityRecord(bool online);

InternetState connectivityState();
bool connectivityAllowsRequests();
bool connectivityConsumeRecovery();
