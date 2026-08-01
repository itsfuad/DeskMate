# 4.5.1

## Removed gradients from webUI

# 4.5.0

## Scenic Weather renderer

- Replaced the Weather background with a compact 4-bit terrain map traced from the supplied reference GIF, preserving its exact mountain, lake, forest, foreground, and tent silhouette while recoloring it at runtime.
- Added explicit dawn and dusk transition palettes between Night/Morning and Afternoon/Evening; all transitions use smooth interpolation around the API-provided sunrise and sunset.
- Added the reference's right-to-left sun arc and optical-axis lens flare, a matching dusk-to-dawn moon arc, and an animated star field.
- Added moving background cloud lanes with condition-dependent cloud density, plus a dedicated main condition icon that remains visible for clear weather.
- Added animated rain/snow particles without allocating a second framebuffer.
- Added stars that fade in and twinkle continuously as darkness increases, while the tent light warms after dark.
- Changed the Weather card to an adaptive light/dark glass tint on one uniform substrate, preventing the lake from creating a separate telemetry strip.
- Added larger two-tone pixel typography with opposite face/shadow contrast for day and night.
- Moved telemetry into the forecast card so the lake remains unobstructed, and limited scenic animation rendering to rows 0–156.
- Added direct preview keys 1/2/3/4 for Clear/Partly Cloudy/Cloudy/Rain cycles and bundled `preview/weather-cycle-demo.gif`.
- Kept the existing streamed HTTPS/ArduinoJson weather transport in the default firmware build rather than introducing an unverified whole-response async client dependency.

Firmware version: 4.5.0

# 4.4.0

## Native 240 × 240 desktop preview

- Added a native Linux preview that directly compiles DeskMate's real Weather, GitHub, Network, Radar, OTA, and tile-rendering source files.
- Added an exact 240 × 240 RGB565 framebuffer with an X11 window enlarged by nearest-neighbour scaling.
- Reused the exact Adafruit GFX primitives and built-in 5 × 7 font resolved by the firmware build.
- Added 26 deterministic fixtures covering normal, loading, busy, error, empty, animated weather cycles, day/night, and OTA progress states.
- Added keyboard navigation, pixel-grid inspection, per-screen BMP capture, and batch screenshot generation.
- Added a headless verification script that builds the preview and validates every generated 240 × 240 screenshot.
- Refactored OTA rendering into a shared `FirmwareUi` renderer used by both the ESP firmware and desktop preview.
- Added RGB565 glass blending for the unified Weather telemetry and forecast card.
- Replaced fixed weather colors with sunrise/sunset-aware Morning, Noon, Afternoon, Evening, and Night palettes blended continuously with smooth interpolation.
- Weather conditions now apply restrained tints instead of replacing the time-of-day palette; partly cloudy and cloudy also have distinct labels/icons.
- Added animated clear, partly cloudy, cloudy, and rain day-cycle previews, plus pause and 30-minute stepping controls.

Firmware version: 4.4.0

## 4.3.6

- Pixel-aligned GitHub stats panel and compact, independently aligned range/total/streak labels.
- Added a readable blended telemetry plate behind Weather feels-like/humidity/wind data.
- Replaced status pulse rings with a simple color-state double-heartbeat LED.
- Visible Network/Radar pages show a solid blue LED before synchronous network work, then resume the state-colored heartbeat.
- Tiny status updates now push only an 11x11 retained region instead of a full 40x40 tile.
- Manual OTA uploads start with an empty left-anchored 0% progress bar; indeterminate center fill is reserved for unknown-length downloads.

# 4.3.2

## 4.3.2 GitHub heatmap geometry fix

- Contribution cells now use the same width and height.
- Cell size is chosen from both graph width and graph height.
- 1, 3, 6, and 12-month grids are centered in a larger calculated graph region.
- Short ranges no longer render as horizontal strips.

## Scalable polling architecture

- Every feature selected in the carousel keeps polling while hidden.
- Rendering and data acquisition are separate: hidden modes update cached snapshots, and only the visible mode touches the display.
- Added one central scheduler for up to ten feature sources.
- Only one network-heavy operation is admitted at a time, preventing concurrent TLS clients and JSON parsers from exhausting ESP8266 heap.
- Missed deadlines are coalesced. A slow source never creates a queue of obsolete historical polls; it keeps one obligation to fetch the newest snapshot.
- Visible, upcoming, forced and continuation jobs receive priority.
- Added deterministic interval jitter so periodic sources do not remain synchronized.
- Added per-source duration prediction, a shared network-duty token bucket, dynamic foreground recovery gaps and overload deferral.
- Added exponential retry backoff and a separate low-frequency path for unconfigured/low-heap skipped work.
- Added scheduler diagnostics to the web status page: completed/failed jobs, coalesced deadlines, budget deferrals, current job, last/average duration and available network credits.

## Browser-assisted validation

- Location search and timezone resolution now happen in the browser through Open-Meteo.
- The browser verifies OpenWeather current and forecast responses before changed weather configuration can be saved.
- The device stores canonical city, country, latitude, longitude, IANA timezone, abbreviation and current UTC offset; normal device weather calls use only coordinates.
- The browser verifies the GitHub token/account and GraphQL contribution access.
- GitHub contribution range can be selected as 1, 3, 6 or 12 months.
- Network and radar settings use bounded one-time device test endpoints when browser CORS/raw TCP limitations prevent direct verification.
- The device repeats cheap structural checks and rejects invalid configuration with HTTP 422.
- At least one carousel feature is always required both in the browser and device settings.

## Reliability and rendering

- Weather and GitHub retain their last successful snapshots through transient failures.
- Weather current and forecast calls are split into scheduler phases so one feature cannot own two back-to-back provider requests as a single opaque task.
- Radar remains static between data updates; the scan animation is removed.
- Boot, setup and recovery screens use measured text, explicit safe regions and compile-time layout assertions for the 240 × 240 panel.
- Browser requests have explicit abort timeouts.
- GitHub's 12-month range is bounded to 365 days.

## Version

Firmware version: 4.3.2


## 4.3.1

- Replaced the unsupported three-argument ESP8266 `WiFiClient::connect` calls.
- Added `platformTcpConnect()`, which applies `setTimeout()` before using the core's supported two-argument connect API.
- Removed the `clockTimeStr()` format-truncation warning by using bounded `strftime()`.
