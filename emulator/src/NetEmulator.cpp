#include "Net.h"
#include "EmulatorPlatform.h"

namespace {
String accessPointSsid = "DeskMate-Setup";
}

void netBegin(const Settings& settings, void (*onProgress)(const char*)) {
  accessPointSsid = settings.apSsid.length() ? settings.apSsid : String("DeskMate-Setup");
  if (onProgress) {
    if (emulatorNetworkMode() == EmulatorNetwork::Sta) onProgress("Host network");
    else if (emulatorNetworkMode() == EmulatorNetwork::Ap) onProgress("Emulated AP");
    else onProgress("Host offline");
  }
}
void netLoop() {}
NetMode netMode() {
  return emulatorNetworkMode() == EmulatorNetwork::Ap ? NET_AP : NET_STA;
}
bool netConnected() { return emulatorNetworkMode() == EmulatorNetwork::Sta; }
String netIP() {
  return emulatorNetworkMode() == EmulatorNetwork::Offline
      ? String("0.0.0.0") : String("127.0.0.1");
}
void netIP(char* output, size_t outputSize) {
  strlcpy(output, netIP().c_str(), outputSize);
}
String netSSID() {
  return emulatorNetworkMode() == EmulatorNetwork::Ap
      ? accessPointSsid : String("Host network");
}
void netSSID(char* output, size_t outputSize) {
  if (!output || !outputSize) return;
  const char* ssid = emulatorNetworkMode() == EmulatorNetwork::Ap
      ? accessPointSsid.c_str() : "Host network";
  strlcpy(output, ssid, outputSize);
}
int netRSSI() {
  return emulatorNetworkMode() == EmulatorNetwork::Sta ? WiFi.RSSI() : 0;
}
