#include "WebPortal.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "webui.h"
#include "Net.h"
#include "Gfx.h"
#include "OtaUpdate.h"
#include "Clock.h"
#include "features/radar/RadarClient.h"
#include <ctype.h>
#include <math.h>

// Defined in main.cpp. Full feature changes invalidate data; display-only
// changes repaint the active mode from cached data without another API poll.
extern void appInvalidate();
extern void appForceRefresh();
extern void appWakeActive();
extern const char* appResetReason();   // last reset reason (diagnostics)
extern void appApplyBrightness();
extern uint32_t appPollCompleted();
extern uint32_t appPollFailed();
extern uint32_t appPollCoalesced();
extern uint32_t appPollDeferrals();
extern uint32_t appPollLastDuration();
extern uint32_t appPollAverageDuration();
extern int32_t appPollCredits();
extern const char* appPollCurrent();

static WebServerClass server(80);
static Settings*        S = nullptr;
static bool             g_reboot = false;
static uint32_t         g_rebootAt = 0;
static bool             g_selfUpdate = false;   // GitHub self-update requested
static String           g_updateMsg;            // last self-update status/error

static void scheduleReboot(uint32_t inMs) {
  g_reboot = true;
  g_rebootAt = millis() + inMs;
}

// ---------------------------------------------------------------------------
static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", WEBUI_HTML);
}

static void handleGetConfig() {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(*S, root, /*includeSecrets=*/false);
  // Which features are compiled in (so a lean build hides the tabs it dropped).
  JsonObject feat = root["features"].to<JsonObject>();
  feat["weather"]=(bool)WITH_WEATHER; feat["network"]=(bool)WITH_NETWORK; feat["radar"]=(bool)WITH_RADAR; feat["github"]=(bool)WITH_GITHUB;
  // Which chip this build runs on (the UI warns about per-chip limitations).
#if defined(DESKMATE_ESP32C2)
  root["chip"] = "esp32c2";
#elif defined(DESKMATE_ESP32)
  root["chip"] = "esp32";
#else
  root["chip"] = "esp8266";
#endif
  sendJson(doc);
}

static void handleStatus() {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["fw"] = FW_NAME;
  o["version"] = FW_VERSION;
  o["repo"] = REPO_URL;
  if (g_updateMsg.length()) o["updateMsg"] = g_updateMsg;
  o["mode"] = (netMode() == NET_AP) ? "ap" : "sta";
  o["connected"] = netConnected();
  o["ssid"] = netSSID();
  o["ip"] = netIP();
  o["rssi"] = netRSSI();
  o["heap"] = ESP.getFreeHeap();
  o["maxblk"] = platformMaxFreeBlock();     // largest contiguous block (TLS handshake needs one)
  o["contstk"] = platformFreeContStack();   // primary stack headroom (ESP8266)
  o["uptime"] = millis() / 1000;
  o["reset"] = appResetReason();
  o["synced"] = clockSynced();
  { String ts = clockTimeStr(); if (ts.length()) o["time"] = ts; }
  o["tz"]        = S->clock.tz;
  o["night"]     = clockNightActive();   // dimming now
  o["nightHeld"] = clockNightHeld();      // in the window but waiting for a fresh NTP sync
  o["clockFresh"] = clockTrusted();
  o["pollCompleted"] = appPollCompleted();
  o["pollFailed"] = appPollFailed();
  o["pollCoalesced"] = appPollCoalesced();
  o["pollDeferrals"] = appPollDeferrals();
  o["pollLastMs"] = appPollLastDuration();
  o["pollAverageMs"] = appPollAverageDuration();
  o["pollCreditsMs"] = appPollCredits();
  o["pollCurrent"] = appPollCurrent();

  sendJson(doc);
}

// Cheap server-side structural validation is still required even though the
// portal performs provider lookups and credential checks in the browser. The
// browser is a usability layer, not a security boundary; direct API callers must
// not be able to persist malformed values that later crash a renderer or client.
static bool validHostLabel(const String& value) {
  if (!value.length() || value.length() > 253 || value.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')) return false;
  }
  return value[0] != '.' && value[value.length() - 1] != '.';
}

static bool validDeviceHostname(const String& value) {
  if (!value.length() || value.length() > 63) return false;
  if (!isalnum(static_cast<unsigned char>(value[0])) ||
      !isalnum(static_cast<unsigned char>(value[value.length() - 1]))) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-')) return false;
  }
  return true;
}

static bool validLatLon(float lat, float lon) {
  return isfinite(lat) && isfinite(lon) && lat >= -90.0f && lat <= 90.0f &&
         lon >= -180.0f && lon <= 180.0f;
}

static bool validHhmm(const char* value) {
  if (!value || strlen(value) != 5 || value[2] != ':') return false;
  if (!isdigit(static_cast<unsigned char>(value[0])) ||
      !isdigit(static_cast<unsigned char>(value[1])) ||
      !isdigit(static_cast<unsigned char>(value[3])) ||
      !isdigit(static_cast<unsigned char>(value[4]))) return false;
  const int hour = (value[0] - '0') * 10 + value[1] - '0';
  const int minute = (value[3] - '0') * 10 + value[4] - '0';
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

static bool validateConfigInput(JsonObjectConst root, String& error) {
  if (root["hostname"].is<const char*>() &&
      !validDeviceHostname(root["hostname"].as<String>())) {
    error = F("invalid hostname"); return false;
  }
  if (root["carouselSec"].is<int>()) {
    const int value = root["carouselSec"].as<int>();
    if (value < 5 || value > 3600) { error = F("invalid carousel interval"); return false; }
  }
  if (root["brightness"].is<int>()) {
    const int value = root["brightness"].as<int>();
    if (value < 0 || value > 100) { error = F("invalid brightness"); return false; }
  }
  if (root["rotation"].is<int>()) {
    const int value = root["rotation"].as<int>();
    if (value < 0 || value > 3) { error = F("invalid rotation"); return false; }
  }

  const bool hasCarouselSelection =
      root["carouselWeather"].is<bool>() ||
      root["carouselNetwork"].is<bool>() ||
      root["carouselRadar"].is<bool>() ||
      root["carouselGithub"].is<bool>();
  if (hasCarouselSelection) {
    // Display controls use partial auto-save requests. Missing members inherit
    // the currently persisted state instead of being interpreted as false.
    const bool weather = root["carouselWeather"].is<bool>()
        ? root["carouselWeather"].as<bool>()
        : (S && S->carouselWeather);
    const bool network = root["carouselNetwork"].is<bool>()
        ? root["carouselNetwork"].as<bool>()
        : (S && S->carouselNetwork);
    const bool radar = root["carouselRadar"].is<bool>()
        ? root["carouselRadar"].as<bool>()
        : (S && S->carouselRadar);
    const bool github = root["carouselGithub"].is<bool>()
        ? root["carouselGithub"].as<bool>()
        : (S && S->carouselGithub);
    if (!weather && !network && !radar && !github) {
      error = F("select at least one carousel screen"); return false;
    }
  }

  if (root["wifi"].is<JsonArrayConst>()) {
    for (JsonObjectConst entry : root["wifi"].as<JsonArrayConst>()) {
      const String ssid = entry["ssid"] | "";
      const String pass = entry["pass"] | "";
      if (!ssid.length() || ssid.length() > 32 || pass.length() > 64) {
        error = F("invalid Wi-Fi credentials"); return false;
      }
    }
  }

  if (root["clock"].is<JsonObjectConst>()) {
    const JsonObjectConst clock = root["clock"].as<JsonObjectConst>();
    if (clock["nightStart"].is<const char*>() &&
        !validHhmm(clock["nightStart"].as<const char*>())) {
      error = F("invalid night start time"); return false;
    }
    if (clock["nightEnd"].is<const char*>() &&
        !validHhmm(clock["nightEnd"].as<const char*>())) {
      error = F("invalid night end time"); return false;
    }
    if (clock["nightLevel"].is<int>()) {
      const int value = clock["nightLevel"].as<int>();
      if (value < 0 || value > 100) { error = F("invalid night brightness"); return false; }
    }
  }

  if (root["weather"].is<JsonObjectConst>()) {
    const JsonObjectConst weather = root["weather"].as<JsonObjectConst>();
    const float lat = weather["lat"] | 999.0f;
    const float lon = weather["lon"] | 999.0f;
    const String city = weather["city"] | "";
    const String timezone = weather["timezone"] | "";
    if (!validLatLon(lat, lon) || !city.length() || city.length() > 80 ||
        !timezone.length() || timezone.length() > 64 ||
        (weather["locationVerified"].is<bool>() &&
         !weather["locationVerified"].as<bool>())) {
      error = F("weather location was not verified"); return false;
    }
    if (weather["pollSec"].is<int>()) {
      const int value = weather["pollSec"].as<int>();
      if (value < 300 || value > 3600) { error = F("invalid weather interval"); return false; }
    }
    if (weather["apiKey"].is<const char*>() &&
        weather["apiKey"].as<String>().length() > 160) {
      error = F("weather key is too long"); return false;
    }
  }

  if (root["network"].is<JsonObjectConst>()) {
    const JsonObjectConst network = root["network"].as<JsonObjectConst>();
    const String probe = network["probeHost"] | "";
    const String dns = network["dnsHost"] | "";
    const int port = network["probePort"] | 0;
    const int interval = network["pollSec"] | 0;
    if (!validHostLabel(probe) || !validHostLabel(dns) || port < 1 || port > 65535 ||
        interval < 3 || interval > 300) {
      error = F("invalid network target"); return false;
    }
  }

  if (root["radar"].is<JsonObjectConst>()) {
    const JsonObjectConst radar = root["radar"].as<JsonObjectConst>();
    const float lat = radar["lat"] | 999.0f;
    const float lon = radar["lon"] | 999.0f;
    const int range = radar["rangeKm"] | 0;
    const int interval = radar["pollSec"] | 0;
    const String source = radar["source"] | "direct";
    const String webhook = radar["webhookUrl"] | "";
    if (!validLatLon(lat, lon) || range < 1 || range > 500 ||
        interval < 3 || interval > 3600 ||
        (source == "webhook" &&
         !(webhook.startsWith("http://") || webhook.startsWith("https://")))) {
      error = F("invalid radar configuration"); return false;
    }
  }

  if (root["github"].is<JsonObjectConst>()) {
    const JsonObjectConst github = root["github"].as<JsonObjectConst>();
    const int months = github["rangeMonths"] | 12;
    const int interval = github["pollSec"] | 0;
    if (!(months == 1 || months == 3 || months == 6 || months == 12) ||
        interval < 300 || interval > 3600) {
      error = F("invalid GitHub range or interval"); return false;
    }
    if (github["token"].is<const char*>() &&
        github["token"].as<String>().length() > 512) {
      error = F("GitHub token is too long"); return false;
    }
  }
  return true;
}

// Fingerprint of everything network-identity related: the WiFi list and the
// hostname. Changing any of it needs a reboot, because the connection and the
// mDNS registration are established once at boot.
static String netFingerprint(const Settings& s) {
  String f((int)s.wifiCount);
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    f += '\n';
    f += s.wifi[i].ssid;
    f += '\x01';
    f += s.wifi[i].pass;
  }
  f += '\n';
  f += s.hostname;
  return f;
}

static void handlePostConfig() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }

  const JsonObjectConst root = doc.as<JsonObjectConst>();
  String validationError;
  if (!validateConfigInput(root, validationError)) {
    JsonDocument response;
    response["ok"] = false;
    response["error"] = validationError;
    sendJson(response, 422);
    return;
  }
  const String oldNet = netFingerprint(*S);
  const uint8_t oldRot = S->rotation;

  // Partial POSTs are used by the display tab for instant controls. Detect which
  // subsystems are actually present so moving a brightness slider does not
  // invalidate every mode or trigger fresh web requests.
  const bool hasClock = root["clock"].is<JsonObjectConst>();
  const bool hasBrightness = root["brightness"].is<int>() ||
                             root["autoBrightness"].is<bool>() ||
                             root["backlightInverted"].is<bool>();
  const bool hasMode = root["mode"].is<const char*>();
  const bool hasRotation = root["rotation"].is<int>();
  const bool hasWeather = root["weather"].is<JsonObjectConst>();
  const bool hasFeatureConfig = hasWeather ||
                                root["network"].is<JsonObjectConst>() ||
                                root["radar"].is<JsonObjectConst>() ||
                                root["github"].is<JsonObjectConst>();

  settingsApplyJson(*S, root);
  const bool persisted = saveSettings(*S);

  // Live apply only what changed. Night mode is evaluated before resolving the
  // PWM target so enabling/disabling it is visible immediately when the clock is
  // already trusted.
  if (hasClock || hasWeather) {
    clockReapply(*S);
    clockService(*S);
  }
  if (hasBrightness || hasClock || hasWeather) appApplyBrightness();
  const bool rotationChanged = hasRotation && S->rotation != oldRot;
  if (rotationChanged) gfxSetRotation(S->rotation);
  if (hasFeatureConfig) appInvalidate();
  else if (hasMode || rotationChanged) appWakeActive();

  const bool wifiChanged = netFingerprint(*S) != oldNet;

  JsonDocument res;
  res["ok"] = persisted;
  res["reboot"] = wifiChanged;
  if (!persisted) res["error"] = "could not save config";
  sendJson(res);

  if (wifiChanged) scheduleReboot(800);
}

static void handleScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 25; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["enc"] = !platformScanIsOpen(i);
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleReboot() {
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

static void handleFactory() {
  factoryReset(*S);
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

// Full settings backup: stream the persisted config.json verbatim. It includes
// the WiFi passwords — same trust domain as typing them into this page.
static void handleExport() {
  File f = LittleFS.open("/config.json", "r");
  if (!f) { server.send(404, "text/plain", "no config saved yet"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=deskmate-config.json");
  server.streamFile(f, "application/json");
  f.close();
}

// Restore a backup: apply everything, persist, reboot (WiFi/hostname may change).
static void handleImport() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  String validationError;
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  if (!validateConfigInput(root, validationError)) {
    server.send(422, "text/plain", validationError);
    return;
  }
  settingsApplyJson(*S, root);
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  scheduleReboot(800);
}

static void handleRefresh() { appForceRefresh(); server.send(200, "application/json", "{\"ok\":true}"); }

// Check the newest GitHub release against the running version.
static void handleCheckUpdate() {
  OtaLatest r = otaCheckLatest(*S);
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["current"] = FW_VERSION;
  o["ok"] = r.ok;
  o["latest"] = r.tag;
  o["newer"] = r.newer;
  if (!r.ok) o["error"] = r.error;
  sendJson(doc);
}

// Trigger the self-update. The actual (blocking) download runs from the loop so
// this response returns first; on success the device reboots into the new image.
static void handleSelfUpdate() {
  g_selfUpdate = true;
  g_updateMsg = "starting...";
  server.send(200, "application/json", "{\"ok\":true}");
}


// ---- one-time configuration tests -----------------------------------------
static void handleTestNetwork() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  const String host = input["probeHost"] | "";
  const String dns = input["dnsHost"] | "";
  const int port = input["probePort"] | 0;
  if (!host.length() || host.length() > 253 || !dns.length() || dns.length() > 253 ||
      port < 1 || port > 65535) {
    server.send(422, "text/plain", "invalid host, DNS name or port"); return;
  }

  IPAddress address;
  const uint32_t dnsStart = millis();
#if defined(DESKMATE_ESP8266)
  const bool dnsOk = WiFi.hostByName(dns.c_str(), address, 1200) == 1;
#else
  const bool dnsOk = WiFi.hostByName(dns.c_str(), address) == 1;
#endif
  const uint32_t dnsMs = millis() - dnsStart;

  WiFiClient client;
  const uint32_t tcpStart = millis();
  const bool tcpOk = platformTcpConnect(
      client, host.c_str(), static_cast<uint16_t>(port), 1200);
  const uint32_t tcpMs = millis() - tcpStart;
  client.stop();

  JsonDocument output;
  output["ok"] = dnsOk && tcpOk;
  output["dnsOk"] = dnsOk;
  output["dnsMs"] = dnsMs;
  output["tcpOk"] = tcpOk;
  output["tcpMs"] = tcpMs;
  output["resolved"] = dnsOk ? address.toString() : String();
  sendJson(output, dnsOk && tcpOk ? 200 : 422);
}

static void handleTestRadar() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  Settings candidate = *S;
  JsonObjectConst wrapper = input.as<JsonObjectConst>();
  if (!wrapper["radar"].is<JsonObjectConst>()) {
    server.send(422, "text/plain", "radar settings required"); return;
  }
  candidate.radar.fromJson(wrapper["radar"].as<JsonObjectConst>());
  uint8_t count = 0;
  int httpCode = 0;
  const bool ok = radarTest(candidate, 4000, count, httpCode);
  JsonDocument output;
  output["ok"] = ok;
  output["aircraft"] = count;
  output["httpCode"] = httpCode;
  if (!ok) output["error"] = "radar endpoint did not return valid aircraft data";
  sendJson(output, ok ? 200 : 422);
}

// ---- OTA ------------------------------------------------------------------
static void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : platformUpdateError().c_str());
  if (ok) scheduleReboot(1200);
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
#if defined(DESKMATE_ESP8266)
    WiFiUDP::stopAll();   // free UDP sockets so the OTA has max contiguous flash/heap
#endif
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.end();
  }
  yield();
}

// ---- captive portal -------------------------------------------------------
static void handleNotFound() {
  if (netMode() == NET_AP) {
    // Redirect everything to the config page so the captive portal pops.
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------------------------------------------------------------------------
void webPortalBegin(Settings& settings) {
  S = &settings;

  // If the last boot ran a queued GitHub update and failed, surface why
  // (success reboots into the new image before we ever get here).
  g_updateMsg = otaTakeBootResult();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/test/network", HTTP_POST, handleTestNetwork);
  server.on("/api/test/radar", HTTP_POST, handleTestRadar);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/import", HTTP_POST, handleImport);
  server.on("/api/checkupdate", HTTP_GET, handleCheckUpdate);
  server.on("/api/selfupdate", HTTP_POST, handleSelfUpdate);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);

  // Common captive-portal probe endpoints
  server.on("/generate_204", handleNotFound);
  server.on("/gen_204", handleNotFound);
  server.on("/hotspot-detect.html", handleNotFound);
  server.on("/connecttest.txt", handleNotFound);
  server.onNotFound(handleNotFound);

  server.begin();
}

void webPortalLoop() {
  server.handleClient();

  // Run the GitHub self-update outside the request handler so the browser gets its
  // response first.
  if (g_selfUpdate) {
    g_selfUpdate = false;
#if defined(DESKMATE_ESP8266)
    // RAM-tight chip: verify there is something to install, then queue the
    // download for the next boot (otaBootUpdate in setup(), ~45 KB free) and
    // reboot. A failure there lands back in g_updateMsg via otaTakeBootResult.
    OtaLatest r = otaCheckLatest(*S);
    if (!r.ok)         g_updateMsg = "check failed: " + r.error;
    else if (!r.newer) g_updateMsg = "already up to date (" FW_VERSION ")";
    else if (otaRequestBootUpdate(r.tag.c_str())) {
      g_updateMsg = "updating...";
      scheduleReboot(400);
    } else {
      g_updateMsg = F("could not queue update (storage error)");
    }
#else
    // ESP32 targets: mbedTLS has the RAM to download in place; blocks while it
    // runs and reboots into the new image on success.
    String err = otaUpdateFromGitHub(*S);
    g_updateMsg = err.length() ? err : "updating...";
#endif
  }
}

bool webPortalRebootDue() {
  return g_reboot && (int32_t)(millis() - g_rebootAt) >= 0;
}
