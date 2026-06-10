#include "Net.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>

static NetMode     g_mode = NET_AP;
static DNSServer   g_dns;
static String      g_hostname;
static String      g_apSsid;
static uint32_t    g_lastReconnect = 0;

static void startAP(const Settings& s) {
  g_mode = NET_AP;
  WiFi.mode(WIFI_AP);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (s.apPass.length() >= 8) {
    WiFi.softAP(s.apSsid.c_str(), s.apPass.c_str());
  } else {
    WiFi.softAP(s.apSsid.c_str());           // open AP (WPA2 needs >=8 chars)
  }
  g_apSsid = s.apSsid;
  // Captive portal: answer every DNS query with our own IP.
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", apIP);
}

void netBegin(const Settings& s, void (*onProgress)(const char*)) {
  g_hostname = s.hostname.length() ? s.hostname : String(DEFAULT_HOSTNAME);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(g_hostname.c_str());

  if (s.staSsid.length() == 0) {
    if (onProgress) onProgress("No WiFi saved");
    startAP(s);
    return;
  }

  if (onProgress) onProgress("Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(s.staSsid.c_str(), s.staPass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    g_mode = NET_STA;
    if (MDNS.begin(g_hostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
    }
    if (onProgress) onProgress(WiFi.localIP().toString().c_str());
  } else {
    if (onProgress) onProgress("WiFi failed -> AP");
    startAP(s);
  }
}

void netLoop() {
  if (g_mode == NET_AP) {
    g_dns.processNextRequest();
    return;
  }
  // STA: keep mDNS alive, nudge reconnect if we dropped.
  MDNS.update();
  if (WiFi.status() != WL_CONNECTED && millis() - g_lastReconnect > 10000) {
    g_lastReconnect = millis();
    WiFi.reconnect();
  }
}

NetMode netMode()      { return g_mode; }
bool    netConnected() { return g_mode == NET_STA && WiFi.status() == WL_CONNECTED; }

String netIP() {
  return (g_mode == NET_AP) ? WiFi.softAPIP().toString()
                            : WiFi.localIP().toString();
}

String netSSID() {
  return (g_mode == NET_AP) ? g_apSsid : WiFi.SSID();
}

int netRSSI() {
  return (g_mode == NET_STA) ? WiFi.RSSI() : 0;
}
