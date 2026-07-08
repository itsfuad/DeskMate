#include "RadarClient.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

static Aircraft g_ac[MAX_AIRCRAFT];   // kept sorted nearest-first
static uint8_t  g_count = 0;
static uint32_t g_lastOkMs = 0;
static bool     g_error = false;
static uint32_t g_nextPollMs = 0;

// TLS receive-buffer size for the adsb.fi handshake, chosen once by probing the
// server's Maximum Fragment Length support. MFLN at 512/1024 lets BearSSL use a
// tiny buffer (a big heap win on the ESP8266); otherwise we fall back to 4 KB and
// hope the records fit — if they don't in busy airspace, the webhook path is the
// reliable alternative.
static uint16_t g_tlsRx = 0;

uint8_t         radarCount()      { return g_count; }
const Aircraft& aircraftAt(uint8_t i) { return g_ac[i]; }
uint32_t        radarLastOkMs()   { return g_lastOkMs; }
bool            radarError()      { return g_error; }

void radarInit(const Settings& s) {
  (void)s;
  g_count = 0;
  g_error = false;
  g_lastOkMs = 0;
  g_nextPollMs = millis();
}

void radarForceRefresh() { g_nextPollMs = millis(); }

// ---- geo: flat-earth projection around home (good enough at radar ranges) --
static void geo(float homeLat, float homeLon, float lat, float lon,
                float& distKm, float& brg) {
  float dLat = (lat - homeLat) * 111.0f;                              // km north
  float dLon = (lon - homeLon) * 111.0f * cosf(homeLat * (float)PI / 180.0f); // km east
  distKm = sqrtf(dLat * dLat + dLon * dLon);
  brg = atan2f(dLon, dLat) * 180.0f / (float)PI;                      // 0 = N, 90 = E
  if (brg < 0) brg += 360.0f;
}

// Keep the array sorted ascending by distance, holding at most MAX_AIRCRAFT.
static void insertNearest(const Aircraft& t) {
  if (g_count == MAX_AIRCRAFT && t.distKm >= g_ac[g_count - 1].distKm) return;
  uint8_t i = (g_count < MAX_AIRCRAFT) ? g_count : (uint8_t)(MAX_AIRCRAFT - 1);
  while (i > 0 && g_ac[i - 1].distKm > t.distKm) { g_ac[i] = g_ac[i - 1]; i--; }
  g_ac[i] = t;
  if (g_count < MAX_AIRCRAFT) g_count++;
}

static void trimTail(char* s) {
  int n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

// ---- probe MFLN once so TLS can use the smallest safe buffer ---------------
static void probeTls() {
  if (g_tlsRx) return;
  if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(ADSB_HOST, 443, 512))       g_tlsRx = 512;
  else if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(ADSB_HOST, 443, 1024)) g_tlsRx = 1024;
  else                                                                              g_tlsRx = 4096;
}

// ---- URL builders ----------------------------------------------------------
static uint16_t rangeNm(uint16_t km) {
  uint16_t nm = (uint16_t)lroundf(km / 1.852f) + 1;   // +1 so the ring edge is covered
  return nm < 1 ? 1 : nm;
}

static String buildDirectUrl(const Settings& s) {
  String u = F("https://");
  u += F(ADSB_HOST);
  u += F(ADSB_PATH);
  u += String(s.radar.lat, 4);
  u += F("/lon/");
  u += String(s.radar.lon, 4);
  u += F("/dist/");
  u += String(rangeNm(s.radar.rangeKm));
  return u;
}

static String buildWebhookUrl(const Settings& s) {
  String u = s.radar.webhookUrl;
  char sep = (u.indexOf('?') >= 0) ? '&' : '?';
  u += sep;
  u += "lat=" + String(s.radar.lat, 4);
  u += "&lon=" + String(s.radar.lon, 4);
  u += "&dist=" + String(s.radar.rangeKm);   // webhook works in km
  return u;
}

// ---- parse the adsb.fi / webhook "ac" array --------------------------------
static bool parseAdsb(const Settings& s, Stream& stream) {
  // Filter to just the fields we plot; applied to every element of "ac".
  JsonDocument filter;
  JsonObject fe = filter["ac"][0].to<JsonObject>();
  fe["lat"] = true;
  fe["lon"] = true;
  fe["track"] = true;
  fe["gs"] = true;
  fe["flight"] = true;
  fe["hex"] = true;
  fe["alt_baro"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonArrayConst ac = doc["ac"].as<JsonArrayConst>();
  if (ac.isNull()) return false;

  g_count = 0;
  for (JsonObjectConst a : ac) {
    if (!a["lat"].is<float>() && !a["lat"].is<int>()) continue;
    if (!a["lon"].is<float>() && !a["lon"].is<int>()) continue;

    Aircraft t;
    t.lat = a["lat"].as<float>();
    t.lon = a["lon"].as<float>();
    t.track = (a["track"].is<float>() || a["track"].is<int>()) ? a["track"].as<float>() : NAN;
    t.gs    = (a["gs"].is<float>()    || a["gs"].is<int>())    ? a["gs"].as<float>()    : NAN;
    t.altFt = a["alt_baro"].is<int>() ? a["alt_baro"].as<int>() : 0;  // "ground" => 0

    // Optional: drop ground/low traffic below the configured altitude threshold.
    if (s.radar.minAltFt > 0 && t.altFt < (int32_t)s.radar.minAltFt) continue;

    const char* fl = a["flight"] | (a["hex"] | "");
    strlcpy(t.callsign, fl, sizeof(t.callsign));
    trimTail(t.callsign);

    geo(s.radar.lat, s.radar.lon, t.lat, t.lon, t.distKm, t.bearingDeg);
    insertNearest(t);
  }

  g_lastOkMs = millis();
  g_error = false;
  return true;
}

// ---- one HTTP(S) GET + parse ----------------------------------------------
static bool fetchUrl(const Settings& s, const String& url) {
  bool https = url.startsWith("https://");

  std::unique_ptr<WiFiClient> client;
  if (https) {
    // TLS needs a big contiguous chunk of heap; skip rather than reset-loop if low.
    if (ESP.getFreeHeap() < 18000) return false;
    probeTls();
    BearSSL::WiFiClientSecure* sc = new BearSSL::WiFiClientSecure();
    sc->setInsecure();                 // no cert validation (public read-only API)
    sc->setBufferSizes(g_tlsRx, 512);
    client.reset(sc);
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");
  http.setUserAgent(F(ADSB_USER_AGENT));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  bool ok = parseAdsb(s, http.getStream());
  http.end();
  return ok;
}

// ---------------------------------------------------------------------------
void radarService(const Settings& s) {
  // No home set yet -> nothing to fetch (the mode shows a prompt instead).
  if (s.radar.lat == 0.0f && s.radar.lon == 0.0f) return;

  if ((int32_t)(millis() - g_nextPollMs) < 0) return;
  g_nextPollMs = millis() + (uint32_t)s.radar.pollSec * 1000UL;

  bool useWebhook = (s.radar.source == RADAR_SRC_WEBHOOK) && (s.radar.webhookUrl.length() >= 8);
  String url = useWebhook ? buildWebhookUrl(s) : buildDirectUrl(s);
  if (!fetchUrl(s, url)) g_error = true;   // keep stale aircraft, flag the error
}
