#include "Connectivity.h"

namespace {
bool g_probeEnabled = false;
InternetState g_state = InternetState::Unknown;
bool g_recovered = false;
}

void connectivityBegin(bool probeEnabled) {
  g_probeEnabled = probeEnabled;
  g_state = InternetState::Unknown;
  g_recovered = false;
}

void connectivityRecord(bool online) {
  const InternetState previous = g_state;
  g_state = online ? InternetState::Online : InternetState::Offline;
  if (!online) g_recovered = false;
  else if (previous == InternetState::Offline) g_recovered = true;
}

InternetState connectivityState() { return g_state; }

bool connectivityAllowsRequests() {
  return !g_probeEnabled || g_state == InternetState::Online;
}

bool connectivityConsumeRecovery() {
  if (!g_recovered || g_state != InternetState::Online) return false;
  g_recovered = false;
  return true;
}
