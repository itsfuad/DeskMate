// config.h — compile-time constants for DeskMate.
#pragma once

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define FW_NAME     "deskmate"
#define FW_VERSION  "4.3.4"

#define REPO_URL      "https://github.com/itsfuad/deskmate"
#define REPO_OWNER    "itsfuad"
#define REPO_NAME     "deskmate"
#if defined(DESKMATE_ESP32C2)
  #define UPDATE_ASSET "deskmate-firmware-c2.bin"
#elif defined(DESKMATE_ESP32)
  #define UPDATE_ASSET "deskmate-firmware-esp32.bin"
#else
  #define UPDATE_ASSET "deskmate-firmware.bin"
#endif
#define GH_API_HOST "api.github.com"

// ---------------------------------------------------------------------------
// Board selection
// ---------------------------------------------------------------------------
#if defined(DESKMATE_ESP32C2)
  #include "board_esp32c2.h"
#elif defined(DESKMATE_ESP32)
  #include "board_esp32.h"
#else
  #include "board_esp8266.h"
#endif

#define TFT_WIDTH  240
#define TFT_HEIGHT 240
#define MAX_WIFI_NETS 4

// ---------------------------------------------------------------------------
// Display modes
// ---------------------------------------------------------------------------
#define MODE_WEATHER   0
#define MODE_NETWORK   1
#define MODE_RADAR     2
#define MODE_GITHUB    3
#define MODE_CAROUSEL  4
#define DEFAULT_MODE MODE_WEATHER
#define DEFAULT_CAROUSEL_SEC 30

#ifndef WITH_RADAR
#define WITH_RADAR 1
#endif
#ifndef WITH_WEATHER
#define WITH_WEATHER 1
#endif
#ifndef WITH_NETWORK
#define WITH_NETWORK 1
#endif
#ifndef WITH_GITHUB
#define WITH_GITHUB 1
#endif

// ---------------------------------------------------------------------------
// Aircraft radar
// ---------------------------------------------------------------------------
#define RADAR_SRC_DIRECT   0
#define RADAR_SRC_WEBHOOK  1
#define DEFAULT_RADAR_SRC  RADAR_SRC_DIRECT
#define ADSB_HOST        "opendata.adsb.fi"
#define ADSB_PATH        "/api/v3/lat/"
#define ADSB_USER_AGENT  "DeskMate/4.3"
#define MAX_AIRCRAFT     24
#define MAX_AIRPORTS      6
#define MAX_ICAO_LEN      8
#define DEFAULT_RADAR_LAT       0.0f
#define DEFAULT_RADAR_LON       0.0f
#define DEFAULT_RADAR_RANGE_KM  20
#define DEFAULT_RADAR_POLL_SEC  10

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------
#define DEFAULT_AP_SSID      "DeskMate-Setup"
#define DEFAULT_AP_PASS      ""
#define DEFAULT_HOSTNAME     "deskmate"
#define DEFAULT_BRIGHTNESS    90
#define DEFAULT_HTTP_TIMEOUT  10000

#define NTP_SERVER1             "pool.ntp.org"
#define NTP_SERVER2             "time.cloudflare.com"
#define DEFAULT_TZ_NAME         ""
#define DEFAULT_TZ_POSIX        "UTC0"
#define DEFAULT_NIGHT_ENABLED   false
#define DEFAULT_NIGHT_START_MIN 1320
#define DEFAULT_NIGHT_END_MIN   420
#define DEFAULT_NIGHT_LEVEL     0
#define DEFAULT_24_HOUR          true
#define NIGHT_NTP_TRUST_MS      300000UL
#define NIGHT_NTP_RESYNC_MS      30000UL

#define DEFAULT_WEATHER_POLL_SEC 600
#define DEFAULT_NETWORK_POLL_SEC 10
#define DEFAULT_GITHUB_POLL_SEC 900
#define GITHUB_GRAPH_WEEKS       53
