#include "WebPortal.h"
#include "Platform.h"
#include <LittleFS.h>
#include "webui.h"
#include "Net.h"
#include "Gfx.h"
#include "OtaUpdate.h"
#include "Clock.h"
#include "features/radar/RadarClient.h"
#include <ctype.h>
#include <math.h>
#include <new>

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

static void closeResponseConnection() {
#if defined(DESKMATE_ESP8266)
  // A retained browser client fragments the small heap and can leave no block
  // large enough for the next BearSSL handshake.
  server.keepAlive(false);
#endif
}

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
class CountingPrint : public Print {
 public:
  size_t write(uint8_t) override { ++length_; return 1; }
  size_t write(const uint8_t*, size_t size) override {
    length_ += size;
    return size;
  }
  size_t length() const { return length_; }

 private:
  size_t length_ = 0;
};

class ServerPrint : public Print {
 public:
  size_t write(uint8_t value) override {
    if (used_ == sizeof(buffer_)) flush();
    buffer_[used_++] = static_cast<char>(value);
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) override {
    size_t written = 0;
    while (written < size) {
      if (used_ == sizeof(buffer_)) flush();
      const size_t available = sizeof(buffer_) - used_;
      const size_t chunk = min(available, size - written);
      memcpy(buffer_ + used_, data + written, chunk);
      used_ += chunk;
      written += chunk;
    }
    return written;
  }
  void flush() {
    if (!used_) return;
    server.sendContent(buffer_, used_);
    used_ = 0;
    yield();
  }

 private:
  char buffer_[256];
  size_t used_ = 0;
};

// ESP8266WebServer is synchronous; all JSON responses can safely share one
// output buffer instead of putting it on the continuation stack.
static ServerPrint g_jsonOutput;

static bool jsonMember(JsonWriter& writer, const char* key, const char* value) {
  return writer.key(key) && writer.value(value);
}
static bool jsonMember(JsonWriter& writer, const char* key, const String& value) {
  return writer.key(key) && writer.value(value);
}
static bool jsonMember(JsonWriter& writer, const char* key, bool value) {
  return writer.key(key) && writer.value(value);
}
template <typename T>
static bool jsonMember(JsonWriter& writer, const char* key, T value) {
  return writer.key(key) && writer.value(value);
}

template <typename WriteJson>
static void writeJsonResponse(int code, WriteJson writeJson) {
  closeResponseConnection();
  size_t length = 0;
  {
    CountingPrint counter;
    JsonWriter writer(counter);
    if (!writeJson(writer) || !writer.complete()) {
      server.send(500, "text/plain", "could not encode response");
      return;
    }
    length = counter.length();
  }

  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.setContentLength(length);
  server.send(code, "application/json", "");
  JsonWriter writer(g_jsonOutput);
  writeJson(writer);
  g_jsonOutput.flush();
}

static void handleRoot() {
  closeResponseConnection();
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
#if defined(DESKMATE_ESP8266)
  // A single 44 KB send can time out after advertising the full Content-Length,
  // leaving the browser with ERR_CONTENT_LENGTH_MISMATCH. Small writes reset the
  // core's send timeout and let lwIP release acknowledged buffers between chunks.
  if (ESP.getFreeHeap() < 4096 || platformMaxFreeBlock() < 2048) {
    server.send(503, "text/plain", "busy; retry shortly");
    return;
  }
  constexpr size_t pageSize = sizeof(WEBUI_HTML) - 1;
  constexpr size_t chunkSize = 1024;
  server.setContentLength(pageSize);
  server.send(200, "text/html", "");
  for (size_t offset = 0; offset < pageSize; offset += chunkSize) {
    const size_t length = min(chunkSize, pageSize - offset);
    server.sendContent_P(WEBUI_HTML + offset, length);
    yield();
  }
#else
  server.send_P(200, "text/html", WEBUI_HTML);
#endif
}

static void handleCrashLog() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", appCrashLog());
}

static void handleGetConfig() {
#if defined(DESKMATE_EMULATOR)
  const char* chip = emulatorBoardProfile().id;
#elif defined(DESKMATE_ESP32C2)
  const char* chip = "esp32c2";
#elif defined(DESKMATE_ESP32)
  const char* chip = "esp32";
#else
  const char* chip = "esp8266";
#endif
  writeJsonResponse(200, [chip](JsonWriter& writer) {
    return writer.beginObject() &&
        settingsWriteJsonFields(writer, *S, false) &&
        writer.key("features") && writer.beginObject() &&
        jsonMember(writer, "weather", static_cast<bool>(WITH_WEATHER)) &&
        jsonMember(writer, "network", static_cast<bool>(WITH_NETWORK)) &&
        jsonMember(writer, "radar", static_cast<bool>(WITH_RADAR)) &&
        jsonMember(writer, "github", static_cast<bool>(WITH_GITHUB)) &&
        writer.endObject() && jsonMember(writer, "chip", chip) &&
        writer.endObject();
  });
}

struct StatusSnapshot {
  char ssid[33];
  char ip[16];
  char time[24];
  const char* updateMsg;
  const char* timezone;
  const char* reset;
  const char* pollCurrent;
  const char* mode;
  bool connected;
  bool synced;
  bool night;
  bool nightHeld;
  bool clockFresh;
  int rssi;
  uint32_t heap;
  uint32_t cpuMhz;
  uint32_t maxBlock;
  uint32_t contStack;
  uint32_t uptime;
  uint32_t pollCompleted;
  uint32_t pollFailed;
  uint32_t pollCoalesced;
  uint32_t pollDeferrals;
  uint32_t pollLastMs;
  uint32_t pollAverageMs;
  int32_t pollCreditsMs;
};

static StatusSnapshot g_status;

static bool writeStatusJson(JsonWriter& writer) {
  const StatusSnapshot& status = g_status;
  if (!writer.beginObject() ||
      !jsonMember(writer, "fw", FW_NAME) ||
      !jsonMember(writer, "version", FW_VERSION) ||
      !jsonMember(writer, "repo", REPO_URL) ||
      (status.updateMsg[0] && !jsonMember(writer, "updateMsg", status.updateMsg)) ||
      !jsonMember(writer, "mode", status.mode) ||
      !jsonMember(writer, "connected", status.connected) ||
      !jsonMember(writer, "ssid", status.ssid) ||
      !jsonMember(writer, "ip", status.ip) ||
      !jsonMember(writer, "rssi", status.rssi) ||
      !jsonMember(writer, "heap", status.heap) ||
      !jsonMember(writer, "cpuMhz", status.cpuMhz) ||
      !jsonMember(writer, "maxblk", status.maxBlock) ||
      !jsonMember(writer, "contstk", status.contStack) ||
      !jsonMember(writer, "uptime", status.uptime) ||
      !jsonMember(writer, "reset", status.reset) ||
      !jsonMember(writer, "synced", status.synced) ||
      (status.time[0] && !jsonMember(writer, "time", status.time)) ||
      !jsonMember(writer, "tz", status.timezone) ||
      !jsonMember(writer, "night", status.night) ||
      !jsonMember(writer, "nightHeld", status.nightHeld) ||
      !jsonMember(writer, "clockFresh", status.clockFresh) ||
      !jsonMember(writer, "pollCompleted", status.pollCompleted) ||
      !jsonMember(writer, "pollFailed", status.pollFailed) ||
      !jsonMember(writer, "pollCoalesced", status.pollCoalesced) ||
      !jsonMember(writer, "pollDeferrals", status.pollDeferrals) ||
      !jsonMember(writer, "pollLastMs", status.pollLastMs) ||
      !jsonMember(writer, "pollAverageMs", status.pollAverageMs) ||
      !jsonMember(writer, "pollCreditsMs", status.pollCreditsMs) ||
      !jsonMember(writer, "pollCurrent", status.pollCurrent)) return false;
  return writer.endObject();
}

static void handleStatus() {
  StatusSnapshot& status = g_status;
  netSSID(status.ssid, sizeof(status.ssid));
  netIP(status.ip, sizeof(status.ip));
  clockTimeStr(*S, status.time, sizeof(status.time));
  status.updateMsg = g_updateMsg.c_str();
  status.timezone = S->clock.tz.c_str();
  status.reset = appResetReason();
  status.pollCurrent = appPollCurrent();
  status.mode = netMode() == NET_AP ? "ap" : "sta";
  status.connected = netConnected();
  status.synced = clockSynced();
  status.night = clockNightActive();
  status.nightHeld = clockNightHeld();
  status.clockFresh = clockTrusted();
  status.rssi = netRSSI();
  status.heap = ESP.getFreeHeap();
  status.cpuMhz = platformCpuFreqMhz();
  status.maxBlock = platformMaxFreeBlock();
  status.contStack = platformFreeContStack();
  status.uptime = millis() / 1000;
  status.pollCompleted = appPollCompleted();
  status.pollFailed = appPollFailed();
  status.pollCoalesced = appPollCoalesced();
  status.pollDeferrals = appPollDeferrals();
  status.pollLastMs = appPollLastDuration();
  status.pollAverageMs = appPollAverageDuration();
  status.pollCreditsMs = appPollCredits();

  writeJsonResponse(200, writeStatusJson);
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

static bool validateConfigInput(const Settings& candidate,
                                const SettingsJsonPresence& presence,
                                String& error) {
  if (presence.hostname && !validDeviceHostname(candidate.hostname)) {
    error = F("invalid hostname"); return false;
  }
  if (presence.carouselSec && !presence.carouselSecValid) {
    error = F("invalid carousel interval"); return false;
  }
  if (presence.brightnessValue && !presence.brightnessValueValid) {
    error = F("invalid brightness"); return false;
  }
  if (presence.rotation && !presence.rotationValid) {
    error = F("invalid rotation"); return false;
  }
  if (presence.carouselSelection && !presence.carouselSelectionValid) {
    error = F("select at least one carousel screen"); return false;
  }
  if (presence.wifi && !presence.wifiEntriesValid) {
    error = F("invalid Wi-Fi credentials"); return false;
  }
  if (presence.clockNightStart && !presence.clockNightStartValid) {
    error = F("invalid night start time"); return false;
  }
  if (presence.clockNightEnd && !presence.clockNightEndValid) {
    error = F("invalid night end time"); return false;
  }
  if (presence.clockUse24HourInvalid) {
    error = F("invalid time format"); return false;
  }
  if (presence.clockNightLevel && !presence.clockNightLevelValid) {
    error = F("invalid night brightness"); return false;
  }

  if (presence.weather) {
    if (!presence.weatherLat || !presence.weatherLon ||
        !presence.weatherCity || !presence.weatherTimezone ||
        !validLatLon(candidate.weather.lat, candidate.weather.lon) ||
        !candidate.weather.city.length() || candidate.weather.city.length() > 80 ||
        !candidate.weather.timezone.length() || candidate.weather.timezone.length() > 64 ||
        (presence.weatherLocationVerified && !candidate.weather.locationVerified)) {
      error = F("weather location was not verified"); return false;
    }
    if (presence.weatherPollSec && !presence.weatherPollSecValid) {
      error = F("invalid weather interval"); return false;
    }
    if (presence.weatherApiKey && !presence.weatherApiKeyValid) {
      error = F("weather key is too long"); return false;
    }
  }

  if (presence.network &&
      (!presence.networkProbeHost || !presence.networkProbePort ||
       !presence.networkDnsHost || !presence.networkPollSec ||
       !validHostLabel(candidate.network.probeHost) ||
       !validHostLabel(candidate.network.dnsHost) ||
       !presence.networkProbePortValid || !presence.networkPollSecValid)) {
    error = F("invalid network target"); return false;
  }

  if (presence.radar) {
    const bool invalidWebhook = presence.radarSource &&
        candidate.radar.source == RADAR_SRC_WEBHOOK &&
        (!presence.radarWebhookUrl ||
         !(candidate.radar.webhookUrl.startsWith("http://") ||
           candidate.radar.webhookUrl.startsWith("https://")));
    if (!presence.radarLat || !presence.radarLon || !presence.radarRangeKm ||
        !presence.radarPollSec ||
        !validLatLon(candidate.radar.lat, candidate.radar.lon) ||
        !presence.radarRangeKmValid || !presence.radarPollSecValid ||
        invalidWebhook) {
      error = F("invalid radar configuration"); return false;
    }
    if (presence.radarAirports && !presence.radarAirportsValid) {
      error = F("invalid radar airport"); return false;
    }
  }

  if (presence.githubRangeMonths && !presence.githubRangeMonthsValid) {
    error = F("GitHub range is fixed at 3 months"); return false;
  }
  if (presence.githubPollSec && !presence.githubPollSecValid) {
    error = F("invalid GitHub interval"); return false;
  }
  if (presence.githubToken && !presence.githubTokenValid) {
    error = F("GitHub token is too long"); return false;
  }
  return true;
}

static bool networkIdentityChanged(const Settings& before,
                                   const Settings& after) {
  if (before.hostname != after.hostname || before.wifiCount != after.wifiCount)
    return true;
  for (uint8_t i = 0; i < before.wifiCount; ++i) {
    if (before.wifi[i].ssid != after.wifi[i].ssid ||
        before.wifi[i].pass != after.wifi[i].pass) return true;
  }
  return false;
}

static bool mutationMemoryReady() {
#if defined(DESKMATE_ESP8266) || defined(DESKMATE_EMULATOR)
  const uint32_t contStack = platformFreeContStack();
  return ESP.getFreeHeap() >= 8192 && platformMaxFreeBlock() >= 1024 &&
         (!contStack || contStack >= 1024);
#else
  return true;
#endif
}

static void __attribute__((noinline)) processPostConfig(const String& body) {
  if (body.length() > JsonScanner::DefaultMaxBytes) {
    server.send(400, "text/plain", "bad json"); return;
  }

  Settings* candidate = new (std::nothrow) Settings();
  if (!candidate) {
    server.send(503, "text/plain", "busy; retry shortly"); return;
  }
  SettingsJsonPresence presence;
  JsonScanner::Error parseError = JsonScanner::Error::None;
  if (!settingsParseJson(*candidate, *S, body.c_str(), body.length(), &presence,
                         &parseError)) {
    delete candidate;
    server.send(parseError == JsonScanner::Error::OutOfMemory ? 503 : 400,
                "text/plain",
                parseError == JsonScanner::Error::OutOfMemory
                    ? "busy; retry shortly" : "bad json");
    return;
  }

  String validationError;
  if (!validateConfigInput(*candidate, presence, validationError)) {
    delete candidate;
    writeJsonResponse(422, [&](JsonWriter& writer) {
      return writer.beginObject() && jsonMember(writer, "ok", false) &&
          jsonMember(writer, "error", validationError) && writer.endObject();
    });
    return;
  }

  const bool wifiChanged = networkIdentityChanged(*S, *candidate);
  const bool rotationChanged = presence.rotation && candidate->rotation != S->rotation;
  const bool timeFormatChanged = presence.clock &&
      candidate->clock.use24Hour != S->clock.use24Hour;
  const bool featureConfig = presence.weather || presence.network || presence.radar ||
                             presence.githubData;
  const bool persisted = saveSettings(*candidate);

  if (persisted) {
    *S = *candidate;
    // Night mode is evaluated before resolving the PWM target so changes are
    // visible immediately when the clock is already trusted.
    if (presence.clock || presence.weather) {
      clockReapply(*S);
      clockService(*S);
    }
    if (presence.brightness || presence.clock || presence.weather)
      appApplyBrightness();
    if (rotationChanged) gfxSetRotation(S->rotation);
    if (featureConfig) appInvalidate();
    else if (presence.mode || rotationChanged || timeFormatChanged ||
             presence.githubDisplay) appWakeActive();
  }
  delete candidate;

  writeJsonResponse(200, [&](JsonWriter& writer) {
    return writer.beginObject() && jsonMember(writer, "ok", persisted) &&
        jsonMember(writer, "reboot", wifiChanged) &&
        (persisted || jsonMember(writer, "error", "could not save config")) &&
        writer.endObject();
  });
  if (persisted && wifiChanged) scheduleReboot(800);
}

static void handlePostConfig() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  if (!mutationMemoryReady()) {
    server.send(503, "text/plain", "busy; retry shortly"); return;
  }
  processPostConfig(server.arg("plain"));
}

static void handleScan() {
  const int count = WiFi.scanNetworks();
  writeJsonResponse(200, [count](JsonWriter& writer) {
    if (!writer.beginArray()) return false;
    for (int i = 0; i < count && i < 25; ++i) {
      if (!writer.beginObject() || !jsonMember(writer, "ssid", WiFi.SSID(i)) ||
          !jsonMember(writer, "rssi", WiFi.RSSI(i)) ||
          !jsonMember(writer, "enc", !platformScanIsOpen(i)) ||
          !writer.endObject()) return false;
    }
    return writer.endArray();
  });
  WiFi.scanDelete();
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

static bool writeFsEntry(JsonWriter& writer, const String& path, size_t size) {
  return writer.beginObject() && jsonMember(writer, "path", path) &&
      jsonMember(writer, "size", static_cast<uint32_t>(size)) &&
      writer.endObject();
}

static bool writeFsList(JsonWriter& writer) {
#if defined(DESKMATE_EMULATOR)
  const size_t total = emulatorFsTotalBytes();
  const size_t used = emulatorFsUsedBytes();
#elif defined(DESKMATE_ESP8266)
  FSInfo info;
  LittleFS.info(info);
  const size_t total = info.totalBytes;
  const size_t used = info.usedBytes;
#else
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
#endif
  if (!writer.beginObject() ||
      !jsonMember(writer, "total", static_cast<uint32_t>(total)) ||
      !jsonMember(writer, "used", static_cast<uint32_t>(used)) ||
      !writer.key("files") || !writer.beginArray()) return false;

#if defined(DESKMATE_EMULATOR)
  for (size_t i = 0; i < emulatorFsFileCount(); ++i) {
    String path;
    size_t size = 0;
    if (emulatorFsFileAt(i, path, size) && !writeFsEntry(writer, path, size))
      return false;
  }
#elif defined(DESKMATE_ESP8266)
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) {
    if (!writeFsEntry(writer, dir.fileName(), dir.fileSize())) return false;
  }
#else
  File rootDir = LittleFS.open("/");
  if (rootDir) {
    File file = rootDir.openNextFile();
    while (file) {
      if (!file.isDirectory() && !writeFsEntry(writer, file.name(), file.size()))
        return false;
      file = rootDir.openNextFile();
    }
  }
#endif
  return writer.endArray() && writer.endObject();
}

static void handleFsList() {
  writeJsonResponse(200, [](JsonWriter& writer) { return writeFsList(writer); });
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

static void writeOkResponse() {
  writeJsonResponse(200, [](JsonWriter& writer) {
    return writer.beginObject() && jsonMember(writer, "ok", true) &&
           writer.endObject();
  });
}

static void handleReboot() {
  writeOkResponse();
  scheduleReboot(400);
}

static void handleFactory() {
  factoryReset(*S);
  saveSettings(*S);
  writeOkResponse();
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
  if (!mutationMemoryReady()) {
    server.send(503, "text/plain", "busy; retry shortly"); return;
  }
  const String body = server.arg("plain");
  if (body.length() > JsonScanner::DefaultMaxBytes) {
    server.send(400, "text/plain", "bad json"); return;
  }

  Settings candidate;
  SettingsJsonPresence presence;
  if (!settingsParseJson(candidate, *S, body.c_str(), body.length(), &presence)) {
    server.send(400, "text/plain", "bad json"); return;
  }
  String validationError;
  if (!validateConfigInput(candidate, presence, validationError)) {
    server.send(422, "text/plain", validationError); return;
  }
  const bool persisted = saveSettings(candidate);
  if (persisted) *S = candidate;
  writeJsonResponse(200, [persisted](JsonWriter& writer) {
    return writer.beginObject() && jsonMember(writer, "ok", persisted) &&
        jsonMember(writer, "reboot", persisted) && writer.endObject();
  });
  if (persisted) scheduleReboot(800);
}

static void handleRefresh() { appForceRefresh(); writeOkResponse(); }

// Check the newest GitHub release against the running version.
static void handleCheckUpdate() {
  const OtaLatest result = otaCheckLatest(*S);
  writeJsonResponse(200, [&](JsonWriter& writer) {
    return writer.beginObject() &&
        jsonMember(writer, "current", FW_VERSION) &&
        jsonMember(writer, "ok", result.ok) &&
        jsonMember(writer, "latest", result.tag) &&
        jsonMember(writer, "newer", result.newer) &&
        (result.ok || jsonMember(writer, "error", result.error)) &&
        writer.endObject();
  });
}

// Trigger the self-update. The actual (blocking) download runs from the loop so
// this response returns first; on success the device reboots into the new image.
static void handleSelfUpdate() {
  g_selfUpdate = true;
  g_updateMsg = "starting...";
  writeOkResponse();
}


// ---- one-time configuration tests -----------------------------------------
struct NetworkTestInput {
  char probeHost[254] = "";
  char dnsHost[254] = "";
  uint16_t probePort = 0;
  uint8_t depth = 0;
  bool rootObject = false;
  bool wrongRoot = false;
  bool valid = true;
};

static void scanNetworkContainer(void* opaque, const JsonScanner&,
                                 JsonScanner::Container event) {
  NetworkTestInput& input = *static_cast<NetworkTestInput*>(opaque);
  const bool start = event == JsonScanner::Container::ObjectStart ||
                     event == JsonScanner::Container::ArrayStart;
  const bool object = event == JsonScanner::Container::ObjectStart ||
                      event == JsonScanner::Container::ObjectEnd;
  if (start) {
    if (!input.depth) {
      input.rootObject = object;
      input.wrongRoot = !object;
    }
    ++input.depth;
  } else if (input.depth) {
    --input.depth;
  }
}

static void scanNetworkValue(void* opaque, const JsonScanner& scanner,
                             JsonScanner::Value type, const char* text,
                             uint32_t number) {
  NetworkTestInput& input = *static_cast<NetworkTestInput*>(opaque);
  if (input.depth != 1) return;
  if (!strcmp(scanner.key(), "probeHost") && type == JsonScanner::Value::String) {
    if (strlen(text) >= sizeof(input.probeHost)) input.valid = false;
    else strlcpy(input.probeHost, text, sizeof(input.probeHost));
  } else if (!strcmp(scanner.key(), "dnsHost") &&
             type == JsonScanner::Value::String) {
    if (strlen(text) >= sizeof(input.dnsHost)) input.valid = false;
    else strlcpy(input.dnsHost, text, sizeof(input.dnsHost));
  } else if (!strcmp(scanner.key(), "probePort") &&
             type == JsonScanner::Value::Number) {
    if (!scanner.numberIsUnsigned() || number < 1 || number > 65535)
      input.valid = false;
    else input.probePort = static_cast<uint16_t>(number);
  }
}

static bool parseNetworkTest(const String& body, NetworkTestInput& input) {
  constexpr size_t maxBody = 1024;
  if (body.length() > maxBody) return false;
  JsonScanner scanner(body.c_str(), body.length());
  scanner.setContainerHandler(scanNetworkContainer, &input);
  return scanner.walk(scanNetworkValue, &input) && input.rootObject &&
         !input.wrongRoot;
}

static void handleTestNetwork() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  if (!mutationMemoryReady()) {
    server.send(503, "text/plain", "busy; retry shortly"); return;
  }
  NetworkTestInput input;
  if (!parseNetworkTest(server.arg("plain"), input)) {
    server.send(400, "text/plain", "bad json"); return;
  }
  if (!input.valid || !input.probeHost[0] || !input.dnsHost[0] ||
      !input.probePort) {
    server.send(422, "text/plain", "invalid host, DNS name or port"); return;
  }

  IPAddress address;
  const uint32_t dnsStart = millis();
#if defined(DESKMATE_ESP8266)
  const bool dnsOk = WiFi.hostByName(input.dnsHost, address, 1200) == 1;
#else
  const bool dnsOk = WiFi.hostByName(input.dnsHost, address) == 1;
#endif
  const uint32_t dnsMs = millis() - dnsStart;

  WiFiClient client;
  const uint32_t tcpStart = millis();
  const bool tcpOk = platformTcpConnect(client, input.probeHost,
                                         input.probePort, 1200);
  const uint32_t tcpMs = millis() - tcpStart;
  client.stop();
  const String resolved = dnsOk ? address.toString() : String();
  const bool ok = dnsOk && tcpOk;
  writeJsonResponse(ok ? 200 : 422, [&](JsonWriter& writer) {
    return writer.beginObject() && jsonMember(writer, "ok", ok) &&
        jsonMember(writer, "dnsOk", dnsOk) && jsonMember(writer, "dnsMs", dnsMs) &&
        jsonMember(writer, "tcpOk", tcpOk) && jsonMember(writer, "tcpMs", tcpMs) &&
        jsonMember(writer, "resolved", resolved) && writer.endObject();
  });
}

static void handleTestRadar() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  if (!mutationMemoryReady()) {
    server.send(503, "text/plain", "busy; retry shortly"); return;
  }
  const String body = server.arg("plain");
  if (body.length() > JsonScanner::DefaultMaxBytes) {
    server.send(400, "text/plain", "bad json"); return;
  }
  Settings candidate;
  SettingsJsonPresence presence;
  if (!settingsParseJson(candidate, *S, body.c_str(), body.length(), &presence)) {
    server.send(400, "text/plain", "bad json"); return;
  }
  if (!presence.radar) {
    server.send(422, "text/plain", "radar settings required"); return;
  }

  uint8_t count = 0;
  int httpCode = 0;
  const bool ok = radarTest(candidate, 8000, count, httpCode);
  char detail[72] = "";
  if (!ok) {
    // An unreachable endpoint, a rate limit and a malformed body are three
    // different problems with three different fixes, so the test names which.
    if (httpCode == 0) {
      strlcpy(detail, "could not reach the radar endpoint", sizeof(detail));
    } else if (httpCode == 429) {
      strlcpy(detail, "radar endpoint is rate limiting this device (HTTP 429)",
              sizeof(detail));
    } else if (httpCode != 200) {
      snprintf(detail, sizeof(detail), "radar endpoint returned HTTP %d", httpCode);
    } else {
      strlcpy(detail, "radar endpoint did not return valid aircraft data",
              sizeof(detail));
    }
  }
  writeJsonResponse(ok ? 200 : 422, [&](JsonWriter& writer) {
    return writer.beginObject() && jsonMember(writer, "ok", ok) &&
        jsonMember(writer, "aircraft", count) &&
        jsonMember(writer, "httpCode", httpCode) &&
        (ok || jsonMember(writer, "error", detail)) && writer.endObject();
  });
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
