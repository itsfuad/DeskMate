#include "StockClient.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

static StockData g_stocks[MAX_SYMBOLS];
static uint8_t   g_count = 0;

static bool     g_refreshing = false;
static uint8_t  g_fetchIdx = 0;
static uint32_t g_nextPollMs = 0;

// ---------------------------------------------------------------------------
void stocksInit(const Settings& s) {
  g_count = s.symbolCount;
  for (uint8_t i = 0; i < g_count; i++) {
    g_stocks[i].clear();
    strlcpy(g_stocks[i].symbol, s.symbols[i].symbol, MAX_SYMBOL_LEN);
    strlcpy(g_stocks[i].name,
            s.symbols[i].name[0] ? s.symbols[i].name : s.symbols[i].symbol,
            MAX_NAME_LEN);
  }
  g_refreshing = false;
  g_nextPollMs = millis();
}

void stocksForceRefresh() {
  g_nextPollMs = millis();
  g_refreshing = false;
}

uint8_t          stocksCount()        { return g_count; }
const StockData& stockAt(uint8_t i)   { return g_stocks[i]; }

bool stocksAnyValid() {
  for (uint8_t i = 0; i < g_count; i++)
    if (g_stocks[i].valid) return true;
  return false;
}

// ---- URL helpers ----------------------------------------------------------
static String urlEncode(const char* src) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (const char* p = src; *p; p++) {
    char c = *p;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

static String buildUrl(const Settings& s, const char* symbol) {
  String url = s.webhookUrl;
  char sep = (url.indexOf('?') >= 0) ? '&' : '?';
  url += sep;
  url += "symbol=" + urlEncode(symbol);
  url += "&range=" + urlEncode(s.range.c_str());
  url += "&points=" + String(s.points);
  return url;
}

// ---- fetch one symbol -----------------------------------------------------
static bool fetchSymbol(const Settings& s, StockData& d) {
  if (s.webhookUrl.length() < 8) return false;

  String url = buildUrl(s, d.symbol);
  bool https = url.startsWith("https://");

  std::unique_ptr<WiFiClient> client;
  if (https) {
    BearSSL::WiFiClientSecure* sc = new BearSSL::WiFiClientSecure();
    sc->setInsecure();                  // no cert validation (LAN/self-host)
    sc->setBufferSizes(4096, 512);      // keep TLS RAM bounded
    client.reset(sc);
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  // Filter so unexpected/large fields don't blow up the heap.
  JsonDocument filter;
  filter["symbol"] = true;
  filter["name"] = true;
  filter["price"] = true;
  filter["currency"] = true;
  filter["change"] = true;
  filter["changePct"] = true;
  filter["range"] = true;
  filter["ok"] = true;
  filter["spark"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) return false;

  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;
  if (!doc["price"].is<float>() && !doc["price"].is<int>()) return false;

  float price = doc["price"].as<float>();
  if (isnan(price)) return false;
  d.price = price;

  if (doc["name"].is<const char*>())
    strlcpy(d.name, doc["name"].as<const char*>(), MAX_NAME_LEN);
  strlcpy(d.currency, doc["currency"] | "", sizeof(d.currency));

  const char* rng = doc["range"] | s.range.c_str();
  strlcpy(d.rangeLabel, rng, sizeof(d.rangeLabel));

  bool hasChg = doc["change"].is<float>() || doc["change"].is<int>();
  bool hasPct = doc["changePct"].is<float>() || doc["changePct"].is<int>();
  d.hasChange = hasChg || hasPct;
  if (hasChg) d.change = doc["change"].as<float>();
  if (hasPct) d.changePct = doc["changePct"].as<float>();
  if (d.hasChange) {
    if (!hasPct && hasChg) {               // derive % from absolute change
      float prev = price - d.change;
      d.changePct = (prev != 0) ? (d.change / prev * 100.0f) : 0;
    } else if (hasPct && !hasChg) {        // derive absolute change from %
      d.change = price * d.changePct / (100.0f + d.changePct);
    }
  }

  d.sparkCount = 0;
  if (doc["spark"].is<JsonArrayConst>()) {
    for (JsonVariantConst v : doc["spark"].as<JsonArrayConst>()) {
      if (d.sparkCount >= MAX_SPARK_POINTS) break;
      d.spark[d.sparkCount++] = v.as<float>();
    }
  }

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

// ---------------------------------------------------------------------------
void stocksService(const Settings& s) {
  if (g_count == 0) return;

  if (!g_refreshing) {
    if ((int32_t)(millis() - g_nextPollMs) >= 0) {
      g_refreshing = true;
      g_fetchIdx = 0;
    } else {
      return;
    }
  }

  // One blocking fetch per call so net/web/display keep getting serviced.
  if (g_fetchIdx < g_count) {
    StockData& d = g_stocks[g_fetchIdx];
    if (!fetchSymbol(s, d)) d.error = true;   // keep stale data, flag error
    g_fetchIdx++;
  }

  if (g_fetchIdx >= g_count) {
    g_refreshing = false;
    uint32_t period = (uint32_t)s.pollSec * 1000UL;
    g_nextPollMs = millis() + period;
  }
}
