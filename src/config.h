// config.h — compile-time constants for smalltv-mod
//
// Hardware: GeekMagic SmallTV, ESP-12F (ESP8266), 1.54" 240x240 ST7789 IPS.
// Pin mapping confirmed from teardowns / ESPHome + Tasmota community configs.
#pragma once

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define FW_NAME     "smalltv-mod"
#define FW_VERSION  "1.0.0"

// ---------------------------------------------------------------------------
// Display wiring (ST7789 240x240 over hardware SPI)
//   ESP8266 HW-SPI fixed pins: SCLK=GPIO14, MOSI=GPIO13.
//   The rest are board-specific GPIO assignments for the SmallTV.
// ---------------------------------------------------------------------------
#define TFT_SCLK   14   // D5  (HW SPI clock, fixed)
#define TFT_MOSI   13   // D7  (HW SPI data,  fixed)
#define TFT_DC      0   // D3  (data/command) — also a boot-strap pin
#define TFT_RST     2   // D4  (reset)        — also a boot-strap pin / onboard LED
#define TFT_CS     15   // D8  (chip select)  — also a boot-strap pin
#define TFT_BL      5   // D1  (backlight, PWM capable)

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// Some SmallTV backlight circuits are active-low. Default active-high; can be
// overridden at runtime via settings (backlightInverted).
#define TFT_BL_DEFAULT_INVERTED false

// Optional ambient light sensor (LDR) on the ADC. Not all units populate it.
// Disabled by default; can be enabled in settings for auto-brightness.
#define LDR_PIN    A0

// ---------------------------------------------------------------------------
// Limits (bound RAM usage on the ESP8266)
// ---------------------------------------------------------------------------
#define MAX_SYMBOLS       8    // max tickers in the rotation
#define MAX_SYMBOL_LEN   16    // e.g. "BTC-USD", "EURUSD=X"
#define MAX_NAME_LEN     20    // friendly name shown on screen
#define MAX_SPARK_POINTS 60    // sparkline samples kept per symbol
#define MAX_URL_LEN     200    // webhook base URL

// ---------------------------------------------------------------------------
// Defaults (used on first boot / factory reset)
// ---------------------------------------------------------------------------
#define DEFAULT_AP_SSID      "SmallTV-Setup"
#define DEFAULT_AP_PASS      ""              // empty => open AP
#define DEFAULT_HOSTNAME     "smalltv"
#define DEFAULT_POLL_SEC      120            // how often to refresh data
#define DEFAULT_ROTATE_SEC    10             // how long each symbol is shown
#define DEFAULT_RANGE        "1d"            // chart timeframe passed to webhook
#define DEFAULT_POINTS        48             // sparkline points requested
#define DEFAULT_BRIGHTNESS    90             // 0..100 %
#define DEFAULT_HTTP_TIMEOUT  8000           // ms per request
