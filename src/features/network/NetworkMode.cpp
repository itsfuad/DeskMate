#include "NetworkMode.h"
#include "Platform.h"
#include "Gfx.h"
#include "TileRenderer.h"
#include "Net.h"
#include <Arduino_GFX_Library.h>

NetworkMode g_networkMode;

namespace {
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t BG       = rgb565(10, 16, 26);
constexpr uint16_t PANEL    = rgb565(18, 27, 40);
constexpr uint16_t LINE     = rgb565(43, 57, 73);
constexpr uint16_t TEXT     = rgb565(236, 241, 247);
constexpr uint16_t MUTED    = rgb565(126, 145, 165);
constexpr uint16_t CYAN     = rgb565(67, 205, 199);
constexpr uint16_t BLUE     = rgb565(91, 150, 230);
constexpr uint16_t CORAL    = rgb565(244, 103, 112);
constexpr uint16_t AMBER    = rgb565(244, 186, 82);
constexpr uint8_t SAMPLE_COUNT = 60;

struct Sample {
  uint16_t tcpMs = 0;
  uint16_t dnsMs = 0;
  bool tcpOk = false;
  bool dnsOk = false;
};

Sample samples[SAMPLE_COUNT];
uint8_t head = 0;
uint8_t count = 0;
bool online = false;
bool haveState = false;
uint32_t outageStart = 0;
uint32_t lastOutageSec = 0;
uint16_t outageCount = 0;

uint8_t ringIndex(uint8_t chronological) {
  return static_cast<uint8_t>((head + SAMPLE_COUNT - count + chronological) % SAMPLE_COUNT);
}

void stats(uint16_t& latest, uint16_t& average, uint16_t& best,
           uint16_t& worst, uint8_t& availability, uint16_t& dnsLatest) {
  latest = average = worst = dnsLatest = 0;
  best = 0xFFFF;
  uint32_t sum = 0;
  uint16_t good = 0;
  uint16_t tcpCount = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const Sample& s = samples[ringIndex(i)];
    if (s.tcpOk && s.dnsOk) ++good;
    if (s.tcpOk) {
      sum += s.tcpMs;
      ++tcpCount;
      best = min(best, s.tcpMs);
      worst = max(worst, s.tcpMs);
    }
  }
  if (count) {
    const Sample& last = samples[(head + SAMPLE_COUNT - 1) % SAMPLE_COUNT];
    latest = last.tcpMs;
    dnsLatest = last.dnsMs;
    availability = static_cast<uint8_t>((good * 100UL) / count);
  } else {
    availability = 0;
  }
  average = tcpCount ? static_cast<uint16_t>(sum / tcpCount) : 0;
  if (best == 0xFFFF) best = 0;
}

uint8_t wifiQuality() {
  const int rssi = netRSSI();
  return static_cast<uint8_t>(constrain((rssi + 90) * 100 / 45, 0, 100));
}

void drawPill(TileCanvas& g, int x, int y, int w, const char* label,
              uint16_t color) {
  g.fillRoundRect(x, y, w, 18, 9, color);
  g.setTextSize(1);
  g.setTextColor(BG);
  g.setCursor(x + (w - gfxTextW(label, 1)) / 2, y + 5);
  g.print(label);
}

void drawCardValue(TileCanvas& g, int x, int y, int w, const char* label,
                   const char* value, uint16_t accent) {
  g.fillRoundRect(x, y, w, 42, 9, PANEL);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(x + 8, y + 7);
  g.print(label);
  g.setTextSize(2);
  g.setTextColor(accent);
  g.setCursor(x + 8, y + 21);
  g.print(value);
}

void drawNetwork(TileCanvas& g, void* opaque) {
  const Settings& settings = *static_cast<const Settings*>(opaque);
  g.fillScreen(BG);
  g.setTextWrap(false);

  uint16_t latest, average, best, worst, dnsLatest;
  uint8_t availability;
  stats(latest, average, best, worst, availability, dnsLatest);

  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(10, 10);
  g.print("NETWORK");
  drawPill(g, 174, 7, 56, online ? "ONLINE" : "OFFLINE",
           online ? CYAN : CORAL);

  g.setTextColor(TEXT);
  g.setTextSize(5);
  char value[32];
  if (online) snprintf(value, sizeof(value), "%u", latest);
  else strlcpy(value, "--", sizeof(value));
  g.setCursor(10, 35);
  g.print(value);
  g.setTextSize(2);
  g.setTextColor(MUTED);
  g.setCursor(141, 63);
  g.print("ms");

  char subtitle[48];
  snprintf(subtitle, sizeof(subtitle), "AVG %u  BEST %u  PEAK %u", average, best, worst);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(12, 84);
  g.print(subtitle);

  // Availability rail.
  g.fillRoundRect(10, 101, 220, 9, 4, LINE);
  g.fillRoundRect(10, 101, static_cast<int>(220UL * availability / 100UL), 9, 4,
                  availability >= 98 ? CYAN : availability >= 90 ? AMBER : CORAL);
  g.setCursor(10, 114);
  g.print("LAST 60 PROBES");
  g.setCursor(185, 114);
  g.setTextColor(TEXT);
  g.print(availability);
  g.print('%');

  // Latency history. A continuous line is easier to read than primitive bars;
  // failed samples are marked as coral outage columns.
  const int gx = 10, gy = 130, gw = 220, gh = 48;
  g.fillRoundRect(gx, gy, gw, gh, 10, PANEL);
  g.drawFastHLine(gx + 8, gy + gh / 2, gw - 16, LINE);
  uint16_t scaleMax = worst > 100 ? worst : 100;
  bool havePrev = false;
  int px = 0, py = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const Sample& s = samples[ringIndex(i)];
    const int x = gx + 7 + static_cast<int>(i * (gw - 14) / (SAMPLE_COUNT - 1));
    if (!s.tcpOk || !s.dnsOk) {
      g.drawFastVLine(x, gy + 6, gh - 12, CORAL);
      havePrev = false;
      continue;
    }
    const int y = gy + gh - 7 - constrain(static_cast<int>(s.tcpMs * (gh - 14) / scaleMax), 1, gh - 14);
    if (havePrev) g.drawLine(px, py, x, y, CYAN);
    g.fillCircle(x, y, 1, CYAN);
    px = x;
    py = y;
    havePrev = true;
  }

  char availText[10], dnsText[12], wifiText[12];
  snprintf(availText, sizeof(availText), "%u%%", availability);
  if (count && samples[(head + SAMPLE_COUNT - 1) % SAMPLE_COUNT].dnsOk)
    snprintf(dnsText, sizeof(dnsText), "%ums", dnsLatest);
  else
    strlcpy(dnsText, "FAIL", sizeof(dnsText));
  snprintf(wifiText, sizeof(wifiText), "%u%%", wifiQuality());
  drawCardValue(g, 10, 187, 68, "UPTIME", availText, CYAN);
  drawCardValue(g, 86, 187, 68, "DNS", dnsText,
                dnsText[0] == 'F' ? CORAL : BLUE);
  drawCardValue(g, 162, 187, 68, "WI-FI", wifiText,
                wifiQuality() > 55 ? CYAN : AMBER);

  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(10, 233);
  if (!online && outageStart) {
    snprintf(value, sizeof(value), "OUTAGE %lus  #%u",
             static_cast<unsigned long>((millis() - outageStart) / 1000UL),
             outageCount);
    g.print(value);
  } else if (lastOutageSec) {
    snprintf(value, sizeof(value), "LAST OUTAGE %lus", static_cast<unsigned long>(lastOutageSec));
    g.print(value);
  } else {
    String host = settings.network.probeHost;
    if (host.length() > 18) host.remove(18);
    g.print(host);
  }
  const String ip = netIP();
  g.setCursor(TFT_WIDTH - gfxTextW(ip.c_str(), 1) - 8, 233);
  g.print(ip);
}
}  // namespace

void NetworkMode::probe(const Settings& settings) {
  Sample sample;

  IPAddress resolved;
  const uint32_t dnsStart = millis();
  sample.dnsOk = WiFi.hostByName(settings.network.dnsHost.c_str(), resolved) == 1;
  const uint32_t dnsElapsed = millis() - dnsStart;
  sample.dnsMs = static_cast<uint16_t>(dnsElapsed > 9999UL ? 9999UL : dnsElapsed);

  WiFiClient client;
  const uint32_t tcpStart = millis();
  sample.tcpOk = client.connect(settings.network.probeHost.c_str(),
                                settings.network.probePort);
  const uint32_t tcpElapsed = millis() - tcpStart;
  client.stop();
  sample.tcpMs = sample.tcpOk
      ? static_cast<uint16_t>(tcpElapsed > 9999UL ? 9999UL : tcpElapsed)
      : 0;

  samples[head] = sample;
  head = static_cast<uint8_t>((head + 1U) % SAMPLE_COUNT);
  if (count < SAMPLE_COUNT) ++count;

  const bool nowOnline = sample.tcpOk && sample.dnsOk;
  if (!haveState) {
    haveState = true;
    online = nowOnline;
    if (!online) {
      outageStart = millis();
      outageCount = 1;
    }
  } else if (nowOnline != online) {
    online = nowOnline;
    if (!online) {
      outageStart = millis();
      ++outageCount;
    } else if (outageStart) {
      lastOutageSec = (millis() - outageStart) / 1000UL;
      outageStart = 0;
    }
  }

  dirty_ = true;
}

void NetworkMode::begin(const Settings&) {
  nextProbe_ = 0;
  dirty_ = true;
}

void NetworkMode::invalidate(const Settings&) {
  nextProbe_ = 0;
  dirty_ = true;
}

void NetworkMode::wake(const Settings&) { dirty_ = true; }

void NetworkMode::render(const Settings& settings) {
  gfxRenderTiled(drawNetwork, const_cast<Settings*>(&settings), BG);
}

void NetworkMode::service(const Settings& settings) {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextProbe_) >= 0) {
    nextProbe_ = now + static_cast<uint32_t>(settings.network.pollSec) * 1000UL;
    probe(settings);
  }
  if (dirty_) {
    render(settings);
    dirty_ = false;
  }
}
