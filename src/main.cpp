// smalltv-mod — custom firmware for the GeekMagic SmallTV (ESP-12F / ESP8266)
//
// Shows live stock / crypto prices (value, % change, sparkline) pulled from an
// n8n webhook, rotating through several tickers. Configured entirely from a
// built-in web UI (WiFi, display, symbols) with OTA firmware updates.
//
// License: WTFPL
#include <Arduino.h>
#include "config.h"
#include "Settings.h"
#include "Net.h"
#include "Display.h"
#include "StockClient.h"
#include "WebPortal.h"

static Settings g_settings;

// rotation / render state
static uint8_t  g_curPage = 0;
static uint32_t g_lastRotate = 0;
static uint32_t g_renderedLastOk = 0xFFFFFFFF;
static bool     g_renderedError = false;
static bool     g_needRender = true;
static uint32_t g_lastAutoBr = 0;

static void bootProgress(const char* msg) {
  displayBootMessage("SmallTV", msg);
}

static void renderCurrent() {
  uint8_t n = stocksCount();
  if (n == 0) {
    displayMessage("No tickers", netIP().c_str(), 0xFFE0);
    return;
  }
  if (g_settings.webhookUrl.length() < 8) {
    displayMessage("Set webhook", netIP().c_str(), 0xFFE0);
    return;
  }
  if (g_curPage >= n) g_curPage = 0;
  displayStock(stockAt(g_curPage), g_curPage, n, g_settings);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION);

  settingsBegin();
  loadSettings(g_settings);

  displayBegin(g_settings);
  displayBootMessage("SmallTV", FW_VERSION);

  netBegin(g_settings, bootProgress);
  webPortalBegin(g_settings);
  stocksInit(g_settings);

  if (netMode() == NET_AP) {
    displayApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(),
                  netIP().c_str());
  } else {
    displayMessage("Connected", netIP().c_str(), 0x07E0);
    delay(1200);
    g_needRender = true;
    g_lastRotate = millis();
  }
}

void loop() {
  netLoop();
  webPortalLoop();

  if (webPortalRebootDue()) {
    delay(120);
    ESP.restart();
  }

  if (netMode() == NET_AP) {
    delay(5);
    return;  // setup mode: AP info stays on screen
  }

  // --- STA mode: fetch + rotate + render ---
  stocksService(g_settings);

  // Auto-brightness (optional LDR on A0)
  if (g_settings.autoBrightness && millis() - g_lastAutoBr > 2000) {
    g_lastAutoBr = millis();
    int raw = analogRead(LDR_PIN);              // 0..1023
    uint8_t pct = (uint8_t)constrain(raw * 100 / 1023, 5, 100);
    displaySetBrightness(pct, g_settings.backlightInverted);
  }

  uint8_t n = stocksCount();

  // Rotate to next ticker
  if (n > 1 && millis() - g_lastRotate >= (uint32_t)g_settings.rotateSec * 1000UL) {
    g_curPage = (g_curPage + 1) % n;
    g_lastRotate = millis();
    g_needRender = true;
  }

  // Re-render when the displayed ticker's data changed
  if (n > 0 && g_curPage < n) {
    const StockData& d = stockAt(g_curPage);
    if (d.lastOkMs != g_renderedLastOk || d.error != g_renderedError) {
      g_needRender = true;
      g_renderedLastOk = d.lastOkMs;
      g_renderedError = d.error;
    }
  }

  if (g_needRender) {
    renderCurrent();
    g_needRender = false;
  }

  delay(5);
}
