// deskmate — custom firmware for the GeekMagic DeskMate (ESP-12F / ESP8266)
//
// Four desk-dashboard features, each a self-contained DisplayMode (see Mode.h):
// ambient weather, network guardian, live ADS-B radar, and GitHub status.
// Shared plumbing (WiFi, web UI, OTA, tiled display core, settings) lives at src root.
//
// License: WTFPL
#include <Arduino.h>
#include "Platform.h"
#include "config.h"
#include "Settings.h"
#include "Net.h"
#include "Gfx.h"
#include "WebPortal.h"
#include "OtaUpdate.h"
#include "Mode.h"
#include "Clock.h"
#include "PollScheduler.h"

#if WITH_WEATHER
#include "WeatherMode.h"
#endif
#if WITH_NETWORK
#include "NetworkMode.h"
#endif
#if WITH_RADAR
#include "RadarMode.h"
#endif
#if WITH_GITHUB
#include "GithubMode.h"
#endif

// ---- mode registry --------------------------------------------------------
// The compiled-in features, in display order. main.cpp holds no per-feature
// state of its own — each mode owns its fetch/render/dirty tracking.
static DisplayMode* kModes[] = {
#if WITH_WEATHER
  &g_weatherMode,
#endif
#if WITH_NETWORK
  &g_networkMode,
#endif
#if WITH_RADAR
  &g_radarMode,
#endif
#if WITH_GITHUB
  &g_githubMode,
#endif
};
static const size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);
static PollScheduler g_pollScheduler;

// ---- carousel -------------------------------------------------------------
// MODE_CAROUSEL rotates through the ticked features. Switches call wake() on
// the incoming mode: repaint from cached data, no refetch.
static size_t   g_carIdx = 0;
static uint32_t g_carSwitch = 0;

static bool carouselHas(const Settings& s, const DisplayMode* m) {
  switch (m->modeConst()) {
    case MODE_WEATHER: return s.carouselWeather;
    case MODE_NETWORK: return s.carouselNetwork;
    case MODE_RADAR: return s.carouselRadar;
    case MODE_GITHUB: return s.carouselGithub;
    default: return true;
  }
}

static bool modePollingEnabled(const Settings& s, const DisplayMode* m) {
  if (s.mode == MODE_CAROUSEL) return carouselHas(s, m);
  return m && m->modeConst() == s.mode;
}

static DisplayMode* upcomingMode(const Settings& s) {
  if (s.mode != MODE_CAROUSEL || !kModeCount) return nullptr;
  for (size_t hop = 1; hop <= kModeCount; ++hop) {
    const size_t candidate = (g_carIdx + hop) % kModeCount;
    if (carouselHas(s, kModes[candidate])) return kModes[candidate];
  }
  return nullptr;
}

// Advance g_carIdx to the next ticked mode (stays put if none other is ticked).
static void carouselNext(const Settings& s) {
  for (size_t hop = 1; hop <= kModeCount; hop++) {
    size_t cand = (g_carIdx + hop) % kModeCount;
    if (!carouselHas(s, kModes[cand])) continue;
    if (cand != g_carIdx) {
      g_carIdx = cand;
      kModes[cand]->wake(s);
    }
    return;
  }
}

static DisplayMode* activeMode(const Settings& s) {
  if (s.mode == MODE_CAROUSEL && kModeCount > 0) {
    if (g_carSwitch == 0) g_carSwitch = millis();
    if (!carouselHas(s, kModes[g_carIdx])) carouselNext(s);   // settings changed
    if (millis() - g_carSwitch >= (uint32_t)s.carouselSec * 1000UL) {
      g_carSwitch = millis();
      carouselNext(s);
    }
    return kModes[g_carIdx];
  }
  for (size_t i = 0; i < kModeCount; i++)
    if (kModes[i]->modeConst() == s.mode) return kModes[i];
  return kModeCount ? kModes[0] : nullptr;   // fall back to the first compiled mode
}

static Settings g_settings;
static String   g_resetReason;        // why the chip last reset (diagnostics)
static bool     g_safeMode = false;   // last reset was an exception -> don't re-enter the crash
static char     g_epcStr[16] = "";
static char     g_addrStr[16] = "";
static int g_lastBr = -1;        // last effective brightness written (-1 = none yet)
#if HAS_LDR
static uint32_t g_lastAutoBr = 0;
static uint8_t  g_ldrCache   = DEFAULT_BRIGHTNESS;   // last LDR reading (2 s cadence)
#endif

// Single brightness resolver: night mode overrides auto-brightness overrides the
// manual level. Only writes the PWM when the effective target changes.
static uint8_t appEffectiveBrightness() {
  if (clockNightActive()) return g_settings.clock.nightLevel;
#if HAS_LDR
  if (g_settings.autoBrightness) {
    if (millis() - g_lastAutoBr > 2000) {
      g_lastAutoBr = millis();
      int raw = analogRead(LDR_PIN);
      g_ldrCache = (uint8_t)constrain(raw * 100 / ADC_MAX, 5, 100);
    }
    return g_ldrCache;
  }
#endif
  return g_settings.brightness;
}

void appApplyBrightness() {
  uint8_t t = appEffectiveBrightness();
  if ((int)t != g_lastBr) {
    g_lastBr = t;
    gfxSetBrightness(t, g_settings.backlightInverted);
  }
}

// Exposed to the web portal (/api/status) so the last reset reason is visible.
const char* appResetReason() { return g_resetReason.c_str(); }

// Called by the web portal after settings are applied: re-init every mode and
// force a fresh repaint so a mode/API/location change takes effect immediately.
void appInvalidate() {
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->invalidate(g_settings);
  g_pollScheduler.forceAll();
}

void appForceRefresh() { g_pollScheduler.forceAll(); }

uint32_t appPollCompleted() { return g_pollScheduler.completedJobs(); }
uint32_t appPollFailed() { return g_pollScheduler.failedJobs(); }
uint32_t appPollCoalesced() { return g_pollScheduler.coalescedDeadlines(); }
uint32_t appPollDeferrals() { return g_pollScheduler.budgetDeferrals(); }
uint32_t appPollLastDuration() { return g_pollScheduler.lastJobDurationMs(); }
uint32_t appPollAverageDuration() { return g_pollScheduler.averageJobDurationMs(); }
int32_t appPollCredits() { return g_pollScheduler.networkCreditsMs(); }
const char* appPollCurrent() { return g_pollScheduler.currentJob(); }

// Lightweight display-only change: repaint the newly selected/current mode from
// its cached data without forcing every feature to poll its web API again.
void appWakeActive() {
  DisplayMode* mode = activeMode(g_settings);
  if (mode) mode->wake(g_settings);
}

static void bootProgress(const char* msg) {
  gfxBoot("DeskMate", msg);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION);

  // Capture why we (re)booted. On a reboot loop this is the key clue, and the
  // device's UART isn't exposed — so we also show it on screen below. On the
  // ESP8266 we also keep the crash PC (epc1) for addr2line decoding; the
  // ESP32-C2 (RISC-V) doesn't expose it, so epc/addr come back empty there.
  PlatformReset pr = platformResetInfo();
  Serial.print("[boot] reset reason: ");
  Serial.println(pr.reason);

  if (pr.wasCrash) {
    g_safeMode = true;                   // crashed last boot -> stay out of the crash path
    strlcpy(g_epcStr,  pr.epc,  sizeof(g_epcStr));
    strlcpy(g_addrStr, pr.addr, sizeof(g_addrStr));
    char rich[80];
    snprintf(rich, sizeof(rich), "%s epc %s addr %s", pr.reason.c_str(),
             g_epcStr[0] ? g_epcStr : "-", g_addrStr[0] ? g_addrStr : "-");
    g_resetReason = rich;
  } else {
    g_resetReason = pr.reason;
  }

  Serial.println("[boot] settings");
  settingsBegin();
  loadSettings(g_settings);

  Serial.println("[boot] display");
  gfxBegin(g_settings);
  gfxBoot(g_safeMode ? "Crashed" : "DeskMate", FW_VERSION);

  Serial.println("[boot] net");
  netBegin(g_settings, bootProgress);
  // Arm SNTP now that Wi-Fi is up. Weather timestamps, GitHub contribution
  // ranges, and optional night mode all depend on a valid wall clock. Skipped after a
  // crash so a fault in here can't boot-loop before the web server starts (the
  // device then comes up in safe mode, OTA-recoverable, instead of needing UART).
  if (!g_safeMode) clockReapply(g_settings);

  // A GitHub update queued from the web UI runs now, before the features claim
  // the heap (the download needs a 16 KB TLS buffer that only fits at boot).
  // On success it reboots into the new image; a no-op stub on the ESP32 targets.
  if (otaBootRequested()) {
    Serial.println("[boot] github update");
    gfxFirmwareUpdate(GfxFirmwareState::Preparing, "GitHub release", 0, 0,
                      "Checking queued update");
    otaBootUpdate(g_settings);
    // HTTP_UPDATE_OK reboots inside otaBootUpdate(). Reaching this line means
    // the attempt failed or the server reported no applicable image.
    gfxFirmwareUpdate(GfxFirmwareState::Failed, "GitHub release", 0, 0,
                      "Update did not complete");
    delay(1400);
    gfxFirmwareUpdateReset();
  }

  Serial.println("[boot] web");
  webPortalBegin(g_settings);

  Serial.println("[boot] modes");
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->begin(g_settings);
  g_pollScheduler.bind(kModes, static_cast<uint8_t>(kModeCount));
  g_pollScheduler.begin(g_settings);
  Serial.println("[boot] done");

  if (netMode() == NET_AP) {
    gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str());
  } else if (g_safeMode) {
    // Last boot crashed: show the crash address (persistent) and keep the web
    // server up for OTA recovery — don't enter the render path that crashed.
    gfxCrash(g_epcStr, g_addrStr, netIP().c_str());
  } else {
    // No second "connected" splash. Move directly from the single boot screen
    // to the first selected feature using its normal tiled renderer.
    DisplayMode* mode = activeMode(g_settings);
    if (mode) {
      mode->wake(g_settings);
      mode->displayTick(g_settings);
    }
  }
}

void loop() {
  netLoop();
  webPortalLoop();

  if (webPortalRebootDue()) {
    delay(120);
    ESP.restart();
  }

  // During an OTA write the display is owned by the retained firmware-update
  // screen. Suspend feature polling and rendering so no carousel tile can
  // overwrite it and no API request competes for heap or network time.
  if (webPortalUpdateActive()) {
    delay(2);
    return;
  }

  if (g_safeMode) {
    delay(5);
    return;  // crashed last boot: web UI stays up for OTA recovery, no rendering
  }

  if (netMode() == NET_AP) {
    delay(5);
    return;  // setup mode: AP info stays on screen
  }

  // --- STA mode: the active feature fetches + renders itself ---

  // Night-mode state machine (NTP-trust gate), then apply the effective brightness
  // (night override / auto-brightness / manual level).
  clockService(g_settings);
  appApplyBrightness();

  DisplayMode* active = activeMode(g_settings);
  DisplayMode* upcoming = upcomingMode(g_settings);

  bool enabled[PollScheduler::MaxModes] = {};
  for (size_t i = 0; i < kModeCount && i < PollScheduler::MaxModes; ++i) {
    enabled[i] = modePollingEnabled(g_settings, kModes[i]);
  }

  // Paint an incoming carousel screen from its cache before a due network job
  // can block. The second tick commits any snapshot that finished in this pass.
  // Both calls are retained/dirty-aware, so a stable screen performs no redraw.
  if (active) active->displayTick(g_settings);

  // One bounded/coalesced background job at a time. All selected carousel
  // sources retain independent schedules; only the visible mode touches LCD RAM.
  g_pollScheduler.service(g_settings, enabled, active, upcoming);

  if (active) active->displayTick(g_settings);
  delay(2);
}
