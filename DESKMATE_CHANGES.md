# DeskMate 4.4.0 changes

## Native 240 × 240 desktop preview

- Added a native Linux preview that directly compiles DeskMate's real Weather, GitHub, Network, Radar, OTA, and tile-rendering source files.
- Added an exact 240 × 240 RGB565 framebuffer with an X11 window enlarged by nearest-neighbour scaling.
- Reused the exact Adafruit GFX primitives and built-in 5 × 7 font resolved by the firmware build.
- Added 22 deterministic fixtures covering normal, loading, busy, error, empty, day/night, and OTA progress states.
- Added keyboard navigation, pixel-grid inspection, per-screen BMP capture, and batch screenshot generation.
- Added a headless verification script that builds the preview and validates every generated 240 × 240 screenshot.
- Refactored OTA rendering into a shared `FirmwareUi` renderer used by both the ESP firmware and desktop preview.
- Added true per-pixel RGB565 translucency for both Weather telemetry and forecast panels.

Firmware version: 4.4.0

## 4.3.6

- Pixel-aligned GitHub stats panel and compact, independently aligned range/total/streak labels.
- Added a readable blended telemetry plate behind Weather feels-like/humidity/wind data.
- Replaced status pulse rings with a simple color-state double-heartbeat LED.
- Visible Network/Radar pages show a solid blue LED before synchronous network work, then resume the state-colored heartbeat.
- Tiny status updates now push only an 11x11 retained region instead of a full 40x40 tile.
- Manual OTA uploads start with an empty left-anchored 0% progress bar; indeterminate center fill is reserved for unknown-length downloads.

# DeskMate 4.3.2 changes

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


## 4.3.1 build compatibility fix

- Replaced the unsupported three-argument ESP8266 `WiFiClient::connect` calls.
- Added `platformTcpConnect()`, which applies `setTimeout()` before using the core's supported two-argument connect API.
- Removed the `clockTimeStr()` format-truncation warning by using bounded `strftime()`.
