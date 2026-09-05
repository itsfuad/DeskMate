#include "OtaUpdate.h"
#include "Platform.h"
#include <LittleFS.h>
#include "config.h"
#include "Gfx.h"
#include "HttpRequest.h"
#include "JsonScanner.h"

#if defined(DESKMATE_ESP32C2) || defined(DESKMATE_ESP32)
#include <HTTPUpdate.h>
#endif

namespace {
constexpr size_t kMaxReleaseBody = 24576;
constexpr size_t kReleaseTagCapacity = 48;
constexpr size_t kReleaseUrlCapacity = 256;

struct ReleaseParse {
  explicit ReleaseParse(const char* wanted) : wantedAsset(wanted) {}

  const char* wantedAsset;
  char tag[kReleaseTagCapacity] = "";
  char matchedUrl[kReleaseUrlCapacity] = "";
  uint8_t depth = 0;
  bool rootObject = false;
  bool assetsArray = false;
  bool assetObject = false;
  bool assetNameMatches = false;
  bool matchedAsset = false;
  bool invalidRelevantValue = false;
};

void releaseContainer(void* context, const JsonScanner& scanner,
                      JsonScanner::Container event) {
  ReleaseParse& parse = *static_cast<ReleaseParse*>(context);
  switch (event) {
    case JsonScanner::Container::ObjectStart:
      ++parse.depth;
      if (parse.depth == 1) {
        parse.rootObject = scanner.container()[0] == 0;
      } else if (parse.depth == 3 && parse.assetsArray &&
                 scanner.container()[0] == 0) {
        parse.assetObject = !parse.matchedAsset;
        parse.assetNameMatches = false;
        if (parse.assetObject) parse.matchedUrl[0] = 0;
      }
      break;

    case JsonScanner::Container::ArrayStart:
      ++parse.depth;
      if (parse.depth == 2 && parse.rootObject &&
          !strcmp(scanner.container(), "assets")) {
        parse.assetsArray = true;
      }
      break;

    case JsonScanner::Container::ObjectEnd:
      if (parse.depth == 3 && parse.assetObject) {
        if (parse.assetNameMatches) parse.matchedAsset = true;
        parse.assetObject = false;
      }
      --parse.depth;
      break;

    case JsonScanner::Container::ArrayEnd:
      if (parse.depth == 2 && parse.assetsArray &&
          !strcmp(scanner.container(), "assets")) {
        parse.assetsArray = false;
      }
      --parse.depth;
      break;
  }
}

void releaseValue(void* context, const JsonScanner& scanner,
                  JsonScanner::Value type, const char* text, uint32_t) {
  ReleaseParse& parse = *static_cast<ReleaseParse*>(context);
  const char* key = scanner.key();
  if (parse.depth == 1 && parse.rootObject && !strcmp(key, "tag_name")) {
    if (type == JsonScanner::Value::String && !scanner.valueTruncated() &&
        scanner.valueLength() < sizeof(parse.tag))
      strlcpy(parse.tag, text, sizeof(parse.tag));
    else
      parse.tag[0] = 0;
    return;
  }
  if (parse.depth != 3 || !parse.assetObject) return;

  if (!strcmp(key, "name")) {
    parse.assetNameMatches = type == JsonScanner::Value::String &&
        !scanner.valueTruncated() && !strcmp(text, parse.wantedAsset);
  } else if (!strcmp(key, "browser_download_url")) {
    if (type == JsonScanner::Value::String && !scanner.valueTruncated() &&
        scanner.valueLength() < sizeof(parse.matchedUrl)) {
      strlcpy(parse.matchedUrl, text, sizeof(parse.matchedUrl));
    } else {
      parse.matchedUrl[0] = 0;
      parse.invalidRelevantValue = true;
    }
  }
}
}  // namespace

// "a.b.c" -> a*10000 + b*100 + c, for a simple newer-than comparison.
static long verNum(const char* v) {
  int a = 0, b = 0, c = 0;
  sscanf(v, "%d.%d.%d", &a, &b, &c);
  return (long)a * 10000 + (long)b * 100 + c;
}

OtaLatest otaCheckLatest(const Settings& s) {
  OtaLatest r;
  if (!platformTlsMemoryReady()) { r.error = F("low heap"); return r; }
  char url[192];
  const int urlLength = snprintf(url, sizeof(url),
                                 "https://%s/repos/%s/%s/releases/latest",
                                 GH_API_HOST, REPO_OWNER, REPO_NAME);
  if (urlLength <= 0 || static_cast<size_t>(urlLength) >= sizeof(url)) {
    r.error = F("release URL too long");
    return r;
  }

  // GitHub over TLS on this chip occasionally stalls a stream read (truncated
  // JSON -> "parse failed") or drops the connection; a couple of quick retries
  // clear the transient. A 403 with the rate-limit budget exhausted is NOT
  // retryable — surface a clear message so the user waits instead of hammering
  // the API (which is what turns an occasional hiccup into a persistent failure).
  const int kAttempts = 3;
  for (int attempt = 1; attempt <= kAttempts; attempt++) {
    OtaLatest candidate;
    bool retryable = false;

    SecureClient client;
    client.setInsecure();
#if defined(DESKMATE_ESP8266)
    client.setBufferSizes(PLATFORM_TLS_RX_BYTES, PLATFORM_TLS_TX_BYTES);
#endif

    // A stalled stream truncates into a "parse failed"; the retries below clear
    // that, so keep the per-attempt timeout modest to stay responsive (this runs
    // in the ESP32 web handler) rather than blocking long on each failing try.
    int code = 0;
    int contentLength = -1;
    bool chunked = false;
    if (!httpGet(client, url, FW_NAME, "application/vnd.github+json",
                 s.httpTimeout, kMaxReleaseBody, &code, &contentLength, &chunked)) {
      candidate.error = F("connect failed"); retryable = true;
    } else {
      if (code == 403) {
        candidate.error = F("GitHub API denied");             // not retryable
      } else if (code != 200 || chunked || contentLength < 0) {
        char status[24];
        snprintf(status, sizeof(status), "HTTP %d", code);
        candidate.error = status;
        retryable = (code >= 500);                             // server-side -> transient
      } else {
        const char* wantedAsset =
#if defined(DESKMATE_EMULATOR)
            emulatorUpdateAsset();
#else
            UPDATE_ASSET;
#endif
        ReleaseParse parsed(wantedAsset);
        static char valueBuffer[kReleaseUrlCapacity];
        Stream& stream = client;
        JsonScanner scanner(stream, client, contentLength, s.httpTimeout,
                            kMaxReleaseBody);
        scanner.setValueBuffer(valueBuffer, sizeof(valueBuffer));
        scanner.setContainerHandler(releaseContainer, &parsed);
        if (!scanner.walk(releaseValue, &parsed)) {
          candidate.error = F("parse failed"); retryable = true;  // truncated/stalled stream
        } else if (parsed.invalidRelevantValue || !parsed.tag[0] ||
                   !parsed.matchedAsset || !parsed.matchedUrl[0]) {
          candidate.error = F("no matching asset");           // not retryable
        } else {
          candidate.tag = parsed.tag;
          candidate.url = parsed.matchedUrl;
          if (candidate.tag.length() != strlen(parsed.tag) ||
              candidate.url.length() != strlen(parsed.matchedUrl)) {
            candidate.tag = "";
            candidate.url = "";
            candidate.error = F("low heap");
            retryable = true;
          } else {
            const char* latest = parsed.tag;
            if (latest[0] == 'v') ++latest;
            candidate.newer = verNum(latest) > verNum(FW_VERSION);
            candidate.ok = true;
          }
        }
      }
      client.stop();
    }

    // Publish one complete attempt; parser callbacks never expose partial fields.
    r = candidate;
    if (r.ok || !retryable) return r;
    if (attempt < kAttempts) delay(500);           // brief backoff before the next try
  }
  return r;   // r.error holds the last (retryable) error after all attempts
}

String otaUpdateFromGitHub(const Settings& s) {
#if defined(DESKMATE_EMULATOR)
  OtaLatest r = otaCheckLatest(s);
  if (!r.ok) return "check failed: " + r.error;
  if (!r.newer) return "already up to date (" FW_VERSION ")";

  SecureClient client;
  client.setInsecure();
  int code = 0;
  int contentLength = -1;
  bool chunked = false;
  if (!httpGet(client, r.url.c_str(), FW_NAME, "application/octet-stream",
               s.httpTimeout, emulatorBoardProfile().otaSlotBytes,
               &code, &contentLength, &chunked) || code != 200 || chunked ||
      contentLength <= 0) {
    client.stop();
    return F("firmware download failed");
  }
  if (!Update.begin(static_cast<uint32_t>(contentLength))) {
    client.stop();
    return platformUpdateError();
  }
  uint8_t buffer[2048];
  uint32_t written = 0;
  gfxFirmwareUpdate(GfxFirmwareState::Downloading, r.tag.c_str(), 0,
                    static_cast<uint32_t>(contentLength));
  while (written < static_cast<uint32_t>(contentLength)) {
    const size_t wanted = min<size_t>(sizeof(buffer), contentLength - written);
    size_t count = 0;
    const uint32_t started = millis();
    while (count < wanted && millis() - started < s.httpTimeout) {
      if (client.available()) {
        const int value = client.read();
        if (value >= 0) buffer[count++] = static_cast<uint8_t>(value);
      } else if (!client.connected()) {
        break;
      } else {
        delay(1);
      }
    }
    if (!count || Update.write(buffer, count) != count) {
      Update.end();
      client.stop();
      return platformUpdateError().length() ? platformUpdateError()
                                            : String("firmware stream ended early");
    }
    written += static_cast<uint32_t>(count);
    gfxFirmwareUpdate(GfxFirmwareState::Writing, r.tag.c_str(), written,
                      static_cast<uint32_t>(contentLength));
  }
  client.stop();
  if (!Update.end(true)) return platformUpdateError();
  gfxFirmwareUpdate(GfxFirmwareState::Complete, r.tag.c_str(), written,
                    static_cast<uint32_t>(contentLength),
                    "Virtual firmware verified - rebooting");
  emulatorRequestRestart();
  return String();
#elif defined(DESKMATE_ESP32C2) || defined(DESKMATE_ESP32)
  OtaLatest r = otaCheckLatest(s);
  if (!r.ok) return "check failed: " + r.error;
  if (!r.newer) return "already up to date (" FW_VERSION ")";
  if (ESP.getFreeHeap() < 22000) return F("not enough free heap for a TLS update");
  gfxFirmwareUpdate(GfxFirmwareState::Downloading, r.tag.c_str(), 0, 0,
                    "Downloading release asset");

  // mbedTLS manages its own buffers; each target pulls its own release asset
  // (UPDATE_ASSET in config.h). The two-slot OTA layout makes this atomic, so a
  // failed/interrupted download just leaves the running image untouched — retry
  // once on a transient stream stall before giving up.
  String lastErr;
  for (int attempt = 1; attempt <= 2; attempt++) {
    SecureClient client;
    client.setInsecure();

    HTTPUpdate up(s.httpTimeout);
    up.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    up.rebootOnUpdate(true);

    t_httpUpdate_return ret = up.update(client, r.url);
    switch (ret) {
      case HTTP_UPDATE_OK:         return "";                     // reboots into the new image
      case HTTP_UPDATE_NO_UPDATES: return F("server reported no update");
      case HTTP_UPDATE_FAILED:     lastErr = up.getLastErrorString(); break;
    }
    if (attempt < 2) {
      gfxFirmwareUpdate(GfxFirmwareState::Downloading, r.tag.c_str(), 0, 0,
                        "Retrying interrupted download");
      delay(1000);
    }
  }
  return "download failed after retry: " + lastErr;
#else
  (void)s;
  return F("internal error: the ESP8266 updates at boot");   // WebPortal never calls this here
#endif
}

// ---- update-at-boot (ESP8266) ----------------------------------------------
// The web UI queues the request in LittleFS and reboots; this runs early in
// setup() with the heap still free. The request is consumed BEFORE the attempt,
// so a crash or failure can never boot-loop.
#if defined(DESKMATE_ESP8266) || defined(DESKMATE_EMULATOR)
static const char* OTA_REQ_PATH = "/ota.req";
static const char* OTA_MSG_PATH = "/ota.msg";

bool otaBootRequested() { return LittleFS.exists(OTA_REQ_PATH); }

bool otaRequestBootUpdate(const char* tag) {
  File f = LittleFS.open(OTA_REQ_PATH, "w");
  if (!f) return false;                     // storage full/broken -> caller must not reboot
  f.print(tag ? tag : "");
  f.close();
  return true;
}

static void otaBootResult(const String& msg) {
  File f = LittleFS.open(OTA_MSG_PATH, "w");
  if (f) { f.print(msg); f.close(); }
}

String otaTakeBootResult() {
  if (!LittleFS.exists(OTA_MSG_PATH)) return String();
  File f = LittleFS.open(OTA_MSG_PATH, "r");
  String m = f ? f.readString() : String();
  if (f) f.close();
  LittleFS.remove(OTA_MSG_PATH);
  return m;
}

void otaBootUpdate(const Settings& s) {
  LittleFS.remove(OTA_REQ_PATH);            // consume first: one attempt per request
  if (WiFi.status() != WL_CONNECTED) { otaBootResult(F("no WiFi at boot")); return; }

#if defined(DESKMATE_EMULATOR)
  const String error = otaUpdateFromGitHub(s);
  if (error.length()) otaBootResult(error);
  return;
#else

  OtaLatest r = otaCheckLatest(s);          // re-resolve the asset URL fresh
  if (!r.ok)    { otaBootResult("check failed: " + r.error); return; }
  if (!r.newer) { otaBootResult(F("already up to date (" FW_VERSION ")")); return; }
  gfxFirmwareUpdate(GfxFirmwareState::Downloading, r.tag.c_str(), 0, 0,
                    "Downloading release asset");

  // Honest guard: rx + tx buffers plus BearSSL engine/stack-thunk overhead.
  const uint32_t need = PLATFORM_TLS_RX_BYTES + PLATFORM_TLS_TX_BYTES +
                        PLATFORM_TLS_HEAP_OVERHEAD_BYTES;
  if (!platformTlsMemoryReady()) {
    otaBootResult("not enough heap even at boot (" + String(ESP.getFreeHeap()) +
                  " free, need " + String(need) + ")");
    return;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(PLATFORM_TLS_RX_BYTES, PLATFORM_TLS_TX_BYTES);

  ESPhttpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  ESPhttpUpdate.setClientTimeout(s.httpTimeout);
  ESPhttpUpdate.rebootOnUpdate(true);

  // Retry once on a transient stream stall — the buffers are still free at boot
  // and the request was already consumed, so a retry can't boot-loop.
  t_httpUpdate_return ret = HTTP_UPDATE_FAILED;
  for (int attempt = 1; attempt <= 2; attempt++) {
    ret = ESPhttpUpdate.update(client, r.url);
    if (ret == HTTP_UPDATE_OK || ret == HTTP_UPDATE_NO_UPDATES) break;  // OK reboots; NO_UPDATES is final
    if (attempt < 2) {
      gfxFirmwareUpdate(GfxFirmwareState::Downloading, r.tag.c_str(), 0, 0,
                        "Retrying interrupted download");
      delay(1000);
    }
  }
  if (ret == HTTP_UPDATE_NO_UPDATES)
    otaBootResult(F("server reported no update"));
  else if (ret != HTTP_UPDATE_OK)
    otaBootResult("download failed: " + ESPhttpUpdate.getLastErrorString());
  // HTTP_UPDATE_OK: rebootOnUpdate restarts into the new image
#endif
}
#else
bool   otaBootRequested() { return false; }
bool   otaRequestBootUpdate(const char*) { return false; }
void   otaBootUpdate(const Settings&) {}
String otaTakeBootResult() { return String(); }
#endif
