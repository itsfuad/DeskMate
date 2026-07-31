#include "Net.h"
#include "PreviewApi.h"

namespace {
int currentRssi = -56;
}

void previewSetNetworkRssi(int rssi) { currentRssi = rssi; }

void netBegin(const Settings&, void (*)(const char*)) {}
void netLoop() {}
NetMode netMode() { return NET_STA; }
bool netConnected() { return true; }
String netIP() { return String("192.168.1.42"); }
String netSSID() { return String("DeskMate Preview"); }
int netRSSI() { return currentRssi; }
