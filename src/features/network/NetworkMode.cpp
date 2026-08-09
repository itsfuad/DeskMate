#include "NetworkMode.h"
#include "Platform.h"
#include "Gfx.h"
#include "TileRenderer.h"
#include "DisplayLayout.h"
#include "Net.h"
#include "StatusDot.h"
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
constexpr uint16_t GREEN    = rgb565(80, 204, 127);
constexpr uint8_t SAMPLE_COUNT = 60;
static_assert(225 - 4 >= DisplayLayout::Left &&
              225 + 4 < DisplayLayout::Right &&
              14 - 4 >= DisplayLayout::Top &&
              14 + 4 < DisplayLayout::Bottom,
              "Network heartbeat must fit the safe display area");

struct Sample {
  uint16_t tcpMs = 0;
  uint16_t dnsMs = 0;
  bool tcpOk = false;
  bool dnsOk = false;
};

struct NetworkRenderContext {
  const Settings* settings = nullptr;
  char host[20] = "";
  char ip[20] = "";
  bool heartbeatOn = false;
  bool pollBusy = false;
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
  return static_cast<uint8_t>((head + SAMPLE_COUNT - count + chronological) %
                              SAMPLE_COUNT);
}

void stats(uint16_t& latest, uint16_t& average, uint16_t& best,
           uint16_t& worst, uint8_t& availability, uint16_t& dnsLatest) {
  latest = average = worst = dnsLatest = 0;
  best = 0xFFFF;
  uint32_t sum = 0;
  uint16_t good = 0;
  uint16_t tcpCount = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const Sample& sample = samples[ringIndex(i)];
    if (sample.tcpOk && sample.dnsOk) ++good;
    if (sample.tcpOk) {
      sum += sample.tcpMs;
      ++tcpCount;
      best = min(best, sample.tcpMs);
      worst = max(worst, sample.tcpMs);
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

uint16_t networkStatusColor() {
  if (!haveState || !count) return BLUE;
  const Sample& last = samples[(head + SAMPLE_COUNT - 1) % SAMPLE_COUNT];
  if (last.tcpOk && last.dnsOk) return GREEN;
  if (last.tcpOk || last.dnsOk) return AMBER;
  return CORAL;
}

void drawNetworkHeartbeat(TileCanvas& g, bool on, bool busy) {
  constexpr int x = 225;
  constexpr int y = 14;
  const uint16_t color = busy ? BLUE : networkStatusColor();
  StatusDot::draw(g, x, y, color, on, busy);
}

struct NetworkLedContext {
  bool on = false;
  bool busy = false;
};

void drawNetworkLedRegion(TileCanvas& g, void* opaque) {
  const NetworkLedContext& context =
      *static_cast<const NetworkLedContext*>(opaque);
  g.fillScreen(BG);
  drawNetworkHeartbeat(g, context.on, context.busy);
}

void drawCardValue(TileCanvas& g, int x, int y, int w, const char* label,
                   const char* value, uint16_t accent) {
  g.fillRoundRect(x, y, w, 40, 9, PANEL);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(x + 8, y + 6);
  g.print(label);
  g.setTextSize(2);
  g.setTextColor(accent);
  g.setCursor(x + 8, y + 20);
  g.print(value);
}

void drawNetwork(TileCanvas& g, void* opaque) {
  const NetworkRenderContext& context =
      *static_cast<const NetworkRenderContext*>(opaque);
  g.fillScreen(BG);
  g.setTextWrap(false);

  uint16_t latest, average, best, worst, dnsLatest;
  uint8_t availability;
  stats(latest, average, best, worst, availability, dnsLatest);

  // All vertical positions are authored against an 8 px safe inset. The last
  // baseline is y=223, so an 8 px font ends at y=230 rather than disappearing
  // under the physical bottom edge.
  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(10, 9);
  g.print("NETWORK GUARDIAN");
  drawNetworkHeartbeat(g, context.heartbeatOn, context.pollBusy);

  g.setTextColor(TEXT);
  g.setTextSize(5);
  char value[40];
  if (online) snprintf(value, sizeof(value), "%u", latest);
  else strlcpy(value, "--", sizeof(value));
  g.setCursor(10, 30);
  g.print(value);
  g.setTextSize(2);
  g.setTextColor(MUTED);
  g.setCursor(70, 50);
  g.print("ms");

  char subtitle[48];
  snprintf(subtitle, sizeof(subtitle), "AVG %u   BEST %u   PEAK %u",
           average, best, worst);
  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(10, 78);
  g.print(subtitle);

  g.fillRoundRect(10, 94, 220, 8, 4, LINE);
  g.fillRoundRect(10, 94,
                  static_cast<int>(220UL * availability / 100UL), 8, 4,
                  availability >= 98 ? CYAN
                  : availability >= 90 ? AMBER : CORAL);
  g.setCursor(10, 106);
  g.print("LAST 60 PROBES");
  char availabilityText[10];
  snprintf(availabilityText, sizeof(availabilityText), "%u%%", availability);
  g.setTextColor(TEXT);
  g.setCursor(230 - gfxTextW(availabilityText, 1), 106);
  g.print(availabilityText);

  constexpr int graphX = 8;
  constexpr int graphY = 121;
  constexpr int graphW = 224;
  constexpr int graphH = 48;
  static_assert(DisplayLayout::fitsSafe(graphX, graphY, graphW, graphH),
                "Network graph must fit the safe display area");
  g.fillRoundRect(graphX, graphY, graphW, graphH, 10, PANEL);
  g.drawFastHLine(graphX + 8, graphY + graphH / 2, graphW - 16, LINE);

  const uint16_t scaleMax = worst > 100 ? worst : 100;
  bool havePrevious = false;
  int previousX = 0;
  int previousY = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const Sample& sample = samples[ringIndex(i)];
    // Keep the newest probe anchored at the right edge. Before the history
    // fills, older samples grow leftward into the available chart space; once
    // full, the ring buffer naturally scrolls the oldest sample off the left.
    const int slot = SAMPLE_COUNT - count + i;
    const int x = graphX + 7 +
                  static_cast<int>(slot * (graphW - 14) / (SAMPLE_COUNT - 1));
    if (!sample.tcpOk || !sample.dnsOk) {
      g.drawFastVLine(x, graphY + 6, graphH - 12, CORAL);
      havePrevious = false;
      continue;
    }
    const int y = graphY + graphH - 7 -
        constrain(static_cast<int>(sample.tcpMs * (graphH - 14) / scaleMax),
                  1, graphH - 14);
    if (havePrevious) g.drawLine(previousX, previousY, x, y, CYAN);
    g.fillCircle(x, y, 1, CYAN);
    previousX = x;
    previousY = y;
    havePrevious = true;
  }

  char dnsText[12];
  char wifiText[12];
  if (count && samples[(head + SAMPLE_COUNT - 1) % SAMPLE_COUNT].dnsOk) {
    snprintf(dnsText, sizeof(dnsText), "%ums", dnsLatest);
  } else {
    strlcpy(dnsText, "FAIL", sizeof(dnsText));
  }
  const uint8_t quality = wifiQuality();
  snprintf(wifiText, sizeof(wifiText), "%u%%", quality);

  constexpr int cardsY = 178;
  static_assert(DisplayLayout::fitsSafe(10, cardsY, 68, 40),
                "Network cards must fit the safe display area");
  drawCardValue(g, 10, cardsY, 68, "UPTIME", availabilityText, CYAN);
  drawCardValue(g, 86, cardsY, 68, "DNS", dnsText,
                dnsText[0] == 'F' ? CORAL : BLUE);
  drawCardValue(g, 162, cardsY, 68, "WI-FI", wifiText,
                quality > 55 ? CYAN : AMBER);

  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(10, 223);
  if (!online && outageStart) {
    snprintf(value, sizeof(value), "OUTAGE %lus  #%u",
             static_cast<unsigned long>((millis() - outageStart) / 1000UL),
             outageCount);
    g.print(value);
  } else if (lastOutageSec) {
    snprintf(value, sizeof(value), "LAST OUTAGE %lus",
             static_cast<unsigned long>(lastOutageSec));
    g.print(value);
  } else {
    g.print(context.host);
  }
  g.setCursor(230 - gfxTextW(context.ip, 1), 223);
  g.print(context.ip);
}
}  // namespace

void NetworkMode::probe(const Settings& settings, uint16_t budgetMs) {
  Sample sample;
  const uint16_t halfBudget = max<uint16_t>(250, budgetMs / 2);

  IPAddress resolved;
  const uint32_t dnsStart = millis();
#if defined(DESKMATE_ESP8266)
  sample.dnsOk = WiFi.hostByName(settings.network.dnsHost.c_str(), resolved,
                                 halfBudget) == 1;
#else
  sample.dnsOk = WiFi.hostByName(settings.network.dnsHost.c_str(), resolved) == 1;
#endif
  const uint32_t dnsElapsed = millis() - dnsStart;
  sample.dnsMs = static_cast<uint16_t>(dnsElapsed > 9999UL ? 9999UL : dnsElapsed);

  WiFiClient client;
  const uint32_t tcpStart = millis();
  sample.tcpOk = platformTcpConnect(client,
                                    settings.network.probeHost.c_str(),
                                    settings.network.probePort, halfBudget);
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

uint32_t NetworkMode::pollIntervalMs(const Settings& settings) const {
  return static_cast<uint32_t>(settings.network.pollSec) * 1000UL;
}

PollResult NetworkMode::poll(const Settings& settings, uint16_t budgetMs) {
  probe(settings, budgetMs);
  // Offline is a valid measurement, not a scheduler failure. Continue at the
  // configured cadence so outages are sampled rather than exponentially backed off.
  return PollResult::Success;
}

void NetworkMode::begin(const Settings&) {
  dirty_ = true;
  heartbeatEpochMs_ = millis();
  heartbeatOn_ = true;
  pollBusy_ = false;
}

void NetworkMode::invalidate(const Settings&) {
  dirty_ = true;
  heartbeatEpochMs_ = millis();
  heartbeatOn_ = true;
  pollBusy_ = false;
}

void NetworkMode::wake(const Settings&) {
  dirty_ = true;
  heartbeatEpochMs_ = millis();
  heartbeatOn_ = true;
  pollBusy_ = false;
}

void NetworkMode::render(const Settings& settings) {
  NetworkRenderContext context;
  context.settings = &settings;
  context.heartbeatOn = heartbeatOn_;
  context.pollBusy = pollBusy_;
  strlcpy(context.host, settings.network.probeHost.c_str(), sizeof(context.host));
  netIP(context.ip, sizeof(context.ip));
  gfxRenderTiled(drawNetwork, &context, BG);
}

void NetworkMode::renderHeartbeat(const Settings&) {
  NetworkLedContext context;
  context.on = heartbeatOn_;
  context.busy = pollBusy_;
  // Push exactly the 11x11 LED region. The dedicated callback avoids rebuilding
  // the full dashboard or allocating temporary Strings before a TLS request.
  gfxRenderRegion(drawNetworkLedRegion, &context, BG, 220, 9, 11, 11);
}

void NetworkMode::pollActivityChanged(const Settings& settings, bool busy) {
  if (pollBusy_ == busy) return;
  pollBusy_ = busy;
  if (!busy) {
    heartbeatEpochMs_ = millis();
    heartbeatOn_ = true;
  }
  renderHeartbeat(settings);
}

void NetworkMode::displayTick(const Settings& settings) {
  const bool nextOn = pollBusy_ ? true
      : StatusDot::onAt(millis(), heartbeatEpochMs_);
  const bool heartbeatChanged = nextOn != heartbeatOn_;
  if (heartbeatChanged) heartbeatOn_ = nextOn;

  if (dirty_) {
    render(settings);
    dirty_ = false;
  } else if (heartbeatChanged) {
    renderHeartbeat(settings);
  }
}
