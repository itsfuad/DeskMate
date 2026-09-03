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
extern const char* appCrashLog();
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
static bool             g_firmwareUpdateActive = false;
static uint32_t         g_updateScreenUntil = 0;
static uint32_t         g_uploadExpected = 0;
static uint32_t         g_uploadWritten = 0;
static uint32_t         g_uploadLastPaint = 0;
static uint8_t          g_uploadLastPercent = 0xFF;
static char             g_uploadName[42] = "";

static void scheduleReboot(uint32_t inMs) {
  g_reboot = true;
  g_rebootAt = millis() + inMs;
}

static void holdFirmwareScreen(uint32_t durationMs) {
  g_firmwareUpdateActive = true;
  g_updateScreenUntil = millis() + durationMs;
}

static void showFirmwareFailure(const char* artifact, const String& message) {
  g_updateMsg = message;
  gfxFirmwareUpdate(GfxFirmwareState::Failed, artifact, g_uploadWritten,
                    g_uploadExpected, message.c_str());
  holdFirmwareScreen(2800);
}

// ---------------------------------------------------------------------------
static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "text/html", WEBUI_HTML);
}

static void handleCrashLog() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", appCrashLog());
}

static void handleGetConfig() {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(*S, root, /*includeSecrets=*/false);
  // Which features are compiled in (so a lean build hides the tabs it dropped).
  JsonObject feat = root["features"].to<JsonObject>();
  feat["weather"]=(bool)WITH_WEATHER; feat["network"]=(bool)WITH_NETWORK; feat["radar"]=(bool)WITH_RADAR; feat["github"]=(bool)WITH_GITHUB;
  // Which chip this build runs on (the UI warns about per-chip limitations).
#if defined(DESKMATE_EMULATOR)
  root["chip"] = emulatorBoardProfile().id;
#elif defined(DESKMATE_ESP32C2)
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
  o["cpuMhz"] = platformCpuFreqMhz();
  o["maxblk"] = platformMaxFreeBlock();     // largest contiguous block (TLS handshake needs one)
  o["contstk"] = platformFreeContStack();   // primary stack headroom (ESP8266)
  o["uptime"] = millis() / 1000;
  o["reset"] = appResetReason();
  o["synced"] = clockSynced();
  { String ts = clockTimeStr(*S); if (ts.length()) o["time"] = ts; }
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

static bool validAirportCode(const char* value) {
  if (!value || !value[0] || strlen(value) >= MAX_ICAO_LEN) return false;
  for (size_t i = 0; value[i]; ++i) {
    if (!isalnum(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
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
    if (!clock["use24Hour"].isNull() && !clock["use24Hour"].is<bool>()) {
      error = F("invalid time format"); return false;
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
    if (radar["airports"].is<JsonArrayConst>()) {
      uint8_t count = 0;
      for (JsonObjectConst airport : radar["airports"].as<JsonArrayConst>()) {
        if (++count > MAX_AIRPORTS || !validAirportCode(airport["icao"] | "") ||
            !validLatLon(airport["lat"] | 999.0f,
                         airport["lon"] | 999.0f)) {
          error = F("invalid radar airport"); return false;
        }
      }
    }
  }

  if (root["github"].is<JsonObjectConst>()) {
    const JsonObjectConst github = root["github"].as<JsonObjectConst>();
    // Validate only the keys the request actually carries. Page selection is
    // posted on its own, and treating an absent interval as zero would reject
    // every such display-only change.
    if (github["rangeMonths"].is<int>() &&
        github["rangeMonths"].as<int>() != 3) {
      error = F("GitHub range is fixed at 3 months"); return false;
    }
    if (github["pollSec"].is<int>()) {
      const int interval = github["pollSec"].as<int>();
      if (interval < 300 || interval > 3600) {
        error = F("invalid GitHub interval"); return false;
      }
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
  const bool oldUse24Hour = S->clock.use24Hour;

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
  // Choosing which GitHub pages rotate is a display preference. Only the keys
  // that change what is fetched count as a feature change, so toggling a page
  // does not discard a good snapshot and re-run the API calls.
  JsonObjectConst github = root["github"];
  const bool hasGithubData = !github.isNull() &&
                             (github["token"].is<const char*>() ||
                              github["login"].is<const char*>() ||
                              github["pollSec"].is<int>());
  const bool hasGithubDisplay = !github.isNull() && !hasGithubData;
  const bool hasFeatureConfig = hasWeather ||
                                root["network"].is<JsonObjectConst>() ||
                                root["radar"].is<JsonObjectConst>() ||
                                hasGithubData;

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
  const bool timeFormatChanged = hasClock && S->clock.use24Hour != oldUse24Hour;
  if (rotationChanged) gfxSetRotation(S->rotation);
  if (hasFeatureConfig) appInvalidate();
  else if (hasMode || rotationChanged || timeFormatChanged || hasGithubDisplay) {
    appWakeActive();
  }

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

static bool validFsPath(const String& path) {
  if (!path.length() || path[0] != '/' || path.length() > 96 ||
      path.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < path.length(); ++i) {
    const char c = path[i];
    if (static_cast<unsigned char>(c) < 32 || c == '\\') return false;
  }
  return path != "/";
}

static void addFsEntry(JsonArray files, const String& path, size_t size) {
  JsonObject entry = files.add<JsonObject>();
  entry["path"] = path;
  entry["size"] = static_cast<uint32_t>(size);
}

static void handleFsList() {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  JsonArray files = root["files"].to<JsonArray>();

#if defined(DESKMATE_EMULATOR)
  root["total"] = emulatorFsTotalBytes();
  root["used"] = emulatorFsUsedBytes();
  for (size_t i = 0; i < emulatorFsFileCount(); ++i) {
    String path;
    size_t size = 0;
    if (emulatorFsFileAt(i, path, size)) addFsEntry(files, path, size);
  }
#elif defined(DESKMATE_ESP8266)
  FSInfo info;
  LittleFS.info(info);
  root["total"] = info.totalBytes;
  root["used"] = info.usedBytes;
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) addFsEntry(files, dir.fileName(), dir.fileSize());
#else
  root["total"] = LittleFS.totalBytes();
  root["used"] = LittleFS.usedBytes();
  File rootDir = LittleFS.open("/");
  if (rootDir) {
    File file = rootDir.openNextFile();
    while (file) {
      if (!file.isDirectory()) addFsEntry(files, file.name(), file.size());
      file = rootDir.openNextFile();
    }
  }
#endif
  sendJson(doc);
}

static void handleFsFile() {
  const String path = server.arg("path");
  if (!validFsPath(path)) {
    server.send(400, "text/plain", "invalid filesystem path");
    return;
  }
  File file = LittleFS.open(path.c_str(), "r");
  if (!file) {
    server.send(404, "text/plain", "file not found");
    return;
  }
  const bool download = server.arg("download") == "1";
  if (download) {
    size_t slash = 0;
    for (size_t i = 0; i < path.length(); ++i) {
      if (path[i] == '/') slash = i;
    }
    String name = path.substring(slash + 1);
    server.sendHeader("Content-Disposition", String("attachment; filename=\"") +
                      name + "\"");
  }
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.streamFile(file, path.endsWith(".json") ? "application/json"
                                                 : "text/plain; charset=utf-8");
  file.close();
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
  const bool ok = radarTest(candidate, 8000, count, httpCode);
  JsonDocument output;
  output["ok"] = ok;
  output["aircraft"] = count;
  output["httpCode"] = httpCode;
  if (!ok) {
    // An unreachable endpoint, a rate limit and a malformed body are three
    // different problems with three different fixes, so the test names which.
    char detail[72];
    if (httpCode == 0) {
      strlcpy(detail, "could not reach the radar endpoint", sizeof(detail));
    } else if (httpCode == 429) {
      strlcpy(detail, "radar endpoint is rate limiting this device (HTTP 429)",
              sizeof(detail));
    } else if (httpCode != 200) {
      snprintf(detail, sizeof(detail), "radar endpoint returned HTTP %d",
               httpCode);
    } else {
      strlcpy(detail, "radar endpoint did not return valid aircraft data",
              sizeof(detail));
    }
    output["error"] = detail;
  }
  sendJson(output, ok ? 200 : 422);
}

// ---- OTA ------------------------------------------------------------------
static void handleUpdateForm() {
  server.send(200, "text/html",
              "<!doctype html><meta name=viewport content='width=device-width'>"
              "<title>DeskMate firmware recovery</title>"
              "<h1>DeskMate firmware recovery</h1>"
              "<p>Keep this device powered while the firmware is uploaded.</p>"
              "<form method=post action=/update enctype=multipart/form-data>"
              "<input type=file name=update accept='.bin,application/octet-stream' required>"
              "<button type=submit>Upload firmware</button></form>");
}

static void handleUpdateDone() {
  const bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  if (ok) {
    gfxFirmwareUpdate(GfxFirmwareState::Complete, g_uploadName,
                      g_uploadWritten, g_uploadExpected,
                      "Firmware verified - rebooting");
    g_firmwareUpdateActive = true;
    g_updateScreenUntil = 0;
    server.send(200, "text/plain", "OK");
    scheduleReboot(1200);
  } else {
    const String error = platformUpdateError();
    server.send(500, "text/plain", error);
    showFirmwareFailure(g_uploadName, error);
  }
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_firmwareUpdateActive = true;
    g_updateScreenUntil = 0;
    g_uploadExpected = static_cast<uint32_t>(strtoul(
        server.arg("size").c_str(), nullptr, 10));
    g_uploadWritten = 0;
    g_uploadLastPaint = 0;
    g_uploadLastPercent = 0xFF;
    strlcpy(g_uploadName,
            up.filename.length() ? up.filename.c_str() : "firmware.bin",
            sizeof(g_uploadName));
    gfxFirmwareUpdate(GfxFirmwareState::Preparing, g_uploadName, 0,
                      g_uploadExpected, "Preparing flash storage");
#if defined(DESKMATE_ESP8266)
    WiFiUDP::stopAll();   // free UDP sockets so the OTA has max contiguous flash/heap
#endif
    const uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace)) {
      Update.printError(Serial);
      showFirmwareFailure(g_uploadName, platformUpdateError());
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.hasError()) {
      showFirmwareFailure(g_uploadName, platformUpdateError());
      yield();
      return;
    }
    const size_t written = Update.write(up.buf, up.currentSize);
    g_uploadWritten += static_cast<uint32_t>(written);
    if (written != up.currentSize) Update.printError(Serial);

    const uint8_t percent = g_uploadExpected
        ? static_cast<uint8_t>(min<uint32_t>(100UL,
            (static_cast<uint64_t>(g_uploadWritten) * 100ULL) /
                g_uploadExpected))
        : 0;
    const uint32_t now = millis();
    if (Update.hasError()) {
      showFirmwareFailure(g_uploadName, platformUpdateError());
    } else {
      const bool changedEnough = g_uploadLastPercent == 0xFF ||
          abs(static_cast<int>(percent) -
              static_cast<int>(g_uploadLastPercent)) >= 3;
      if (percent == 100 ||
          (changedEnough && now - g_uploadLastPaint >= 320UL)) {
        g_uploadLastPaint = now;
        g_uploadLastPercent = percent;
        gfxFirmwareUpdate(GfxFirmwareState::Writing, g_uploadName,
                          g_uploadWritten, g_uploadExpected);
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    gfxFirmwareUpdate(GfxFirmwareState::Verifying, g_uploadName,
                      g_uploadWritten, g_uploadExpected,
                      "Checking firmware image");
    if (!Update.end(true)) {
      Update.printError(Serial);
      showFirmwareFailure(g_uploadName, platformUpdateError());
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    showFirmwareFailure(g_uploadName, F("Upload was aborted"));
  }
  yield();
}

// ---- captive portal -------------------------------------------------------
static void handleNotFound() {
  if (netMode() == NET_AP) {
    // Redirect everything to the config page so the captive portal pops.
    server.sendHeader("Cache-Control", "no-store, max-age=0");
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
  server.on("/crashlog", HTTP_GET, handleCrashLog);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/fs", HTTP_GET, handleFsList);
  server.on("/api/fs/file", HTTP_GET, handleFsFile);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/test/network", HTTP_POST, handleTestNetwork);
  server.on("/api/test/radar", HTTP_POST, handleTestRadar);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/import", HTTP_POST, handleImport);
  server.on("/api/checkupdate", HTTP_GET, handleCheckUpdate);
  server.on("/api/selfupdate", HTTP_POST, handleSelfUpdate);
  server.on("/update", HTTP_GET, handleUpdateForm);
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
    g_firmwareUpdateActive = true;
    g_updateScreenUntil = 0;
    g_uploadExpected = 0;
    g_uploadWritten = 0;
    gfxFirmwareUpdate(GfxFirmwareState::Preparing, "GitHub release", 0, 0,
                      "Checking latest release");
#if defined(DESKMATE_ESP8266)
    // RAM-tight chip: verify there is something to install, then queue the
    // download for the next boot (otaBootUpdate in setup(), ~45 KB free) and
    // reboot. A failure there lands back in g_updateMsg via otaTakeBootResult.
    OtaLatest r = otaCheckLatest(*S);
    if (!r.ok) {
      showFirmwareFailure("GitHub release", "check failed: " + r.error);
    } else if (!r.newer) {
      g_updateMsg = "already up to date (" FW_VERSION ")";
      gfxFirmwareUpdate(GfxFirmwareState::Current, r.tag.c_str(), 0, 0,
                        g_updateMsg.c_str());
      holdFirmwareScreen(1800);
    } else if (otaRequestBootUpdate(r.tag.c_str())) {
      g_updateMsg = "restarting for update";
      gfxFirmwareUpdate(GfxFirmwareState::Preparing, r.tag.c_str(), 0, 0,
                        "Restarting with free update memory");
      scheduleReboot(500);
    } else {
      showFirmwareFailure(r.tag.c_str(),
                          F("could not queue update (storage error)"));
    }
#elif defined(DESKMATE_EMULATOR)
    if (emulatorBoardProfile().hasLdr) {
      OtaLatest r = otaCheckLatest(*S);
      if (!r.ok) {
        showFirmwareFailure("GitHub release", "check failed: " + r.error);
      } else if (!r.newer) {
        g_updateMsg = "already up to date (" FW_VERSION ")";
        gfxFirmwareUpdate(GfxFirmwareState::Current, r.tag.c_str(), 0, 0,
                          g_updateMsg.c_str());
        holdFirmwareScreen(1800);
      } else if (otaRequestBootUpdate(r.tag.c_str())) {
        g_updateMsg = "restarting for update";
        gfxFirmwareUpdate(GfxFirmwareState::Preparing, r.tag.c_str(), 0, 0,
                          "Restarting with free update memory");
        scheduleReboot(500);
      } else {
        showFirmwareFailure(r.tag.c_str(),
                            F("could not queue update (storage error)"));
      }
    } else {
      String err = otaUpdateFromGitHub(*S);
      if (err.length()) showFirmwareFailure("GitHub release", err);
      else g_updateMsg = "updating...";
    }
#else
    // ESP32 targets: mbedTLS has the RAM to download in place; blocks while it
    // runs and reboots into the new image on success.
    String err = otaUpdateFromGitHub(*S);
    if (err.length()) showFirmwareFailure("GitHub release", err);
    else g_updateMsg = "updating...";
#endif
  }
}

bool webPortalRebootDue() {
  return g_reboot && (int32_t)(millis() - g_rebootAt) >= 0;
}

bool webPortalUpdateActive() {
  if (g_firmwareUpdateActive && g_updateScreenUntil &&
      static_cast<int32_t>(millis() - g_updateScreenUntil) >= 0) {
    g_firmwareUpdateActive = false;
    g_updateScreenUntil = 0;
    gfxFirmwareUpdateReset();
    appWakeActive();
  }
  return g_firmwareUpdateActive;
}
