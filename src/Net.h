// Net.h — WiFi station / fallback AP / captive portal / mDNS
#pragma once
#include <Arduino.h>
#include "Settings.h"

enum NetMode { NET_STA, NET_AP };

// Connects to the configured station; falls back to AP if that fails or no
// credentials are stored. `onProgress` (optional) is called with short status
// strings so the display can show what's happening during the boot connect.
void netBegin(const Settings& s, void (*onProgress)(const char*) = nullptr);
void netLoop();           // pump DNS (AP) / mDNS (STA) / reconnect

NetMode  netMode();
bool     netConnected();  // STA associated with an IP
String   netIP();         // current IP (STA or AP)
String   netSSID();       // joined SSID (STA) or AP SSID
int      netRSSI();       // STA signal, 0 in AP mode
