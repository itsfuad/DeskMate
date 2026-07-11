// loader.cpp — a minimal OTA "trampoline" for devices whose stock firmware
// reserves so much flash that its updater rejects a full smalltv-mod image
// ("Not Enough Space"), e.g. the GeekMagic SmallTV-ultra with its large
// image/GIF store.
//
// It is deliberately tiny: WiFi + a bare web OTA endpoint, no display, no
// features. Small enough to fit the stock firmware's slot, it is flashed over
// the stock OTA. Once running it uses smalltv-mod's normal 4m1m flash layout,
// whose sketch region is large, so ITS /update then accepts the full firmware
// the stock updater refused. Two hops, no UART needed.
//
// WiFi creds are compile-time only (-DLOADER_SSID / -DLOADER_PASS), never in
// the repo. Without them it falls back to an open "SmallTV-Loader" AP at
// 192.168.4.1 so the full image can still be uploaded by hand.
#ifdef LOADER_BUILD
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#ifndef LOADER_SSID
#define LOADER_SSID ""
#endif
#ifndef LOADER_PASS
#define LOADER_PASS ""
#endif

static ESP8266WebServer       server(80);
static ESP8266HTTPUpdateServer updater;

void setup() {
  Serial.begin(115200);
  Serial.println(F("\nsmalltv-mod loader"));
  WiFi.persistent(false);

  const char* ssid = LOADER_SSID;
  bool joined = false;
  if (ssid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.hostname("smalltv-loader");
    WiFi.begin(LOADER_SSID, LOADER_PASS);
    for (int i = 0; i < 120 && WiFi.status() != WL_CONNECTED; i++) { delay(250); yield(); }
    joined = (WiFi.status() == WL_CONNECTED);
  }
  if (!joined) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SmallTV-Loader");        // open AP -> browse to 192.168.4.1/update
  }

  Serial.println(joined ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
  updater.setup(&server);                 // serves /update (GET form + POST handler)
  server.begin();
}

void loop() {
  server.handleClient();
  yield();
}
#endif  // LOADER_BUILD
