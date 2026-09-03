# Changelog

## Unreleased

### GitHub screen

- Reframed the screen around pending work. Up to three pages rotate: an inbox of review requests, mentions and assigned issues; the viewer's open pull requests with review-decision and CI-check badges; and the previous counts, streak and contribution heatmap as a pulse page.
- Made the pages individually selectable in the web UI's GitHub tab, with at least one always kept. The portal shows the resulting per-page dwell as the selection or the carousel dwell changes.
- Replaced the fixed eight-second page interval with one derived from the screen's own display window: the carousel dwell divided by the number of selected pages, floored at 1.2 s so a short window cannot thrash the panel with full repaints. One carousel visit now covers the rotation exactly once instead of stopping wherever a fixed interval landed.
- Toggling page selection no longer counts as a feature change in the configuration API, so it repaints without discarding a good snapshot and re-running the GitHub API calls.
- Fixed GitHub configuration validation rejecting any partial POST: an absent `pollSec` was read as zero and failed the range check, so every request that did not resend the whole object was refused. Only the keys a request carries are validated now.
- Coloured rows with GitHub's own state palette rather than by category: open green, draft grey, merged purple, closed red (Primer's dark-theme values). The icon carries what a row is and the colour carries what state it is in, which is how GitHub's own inbox reads. Pull-request rows pick their glyph to match — `git-pull-request`, `git-pull-request-draft`, `git-merge`, `git-pull-request-closed` — and a closed issue shows a check rather than a dot.
- The author search no longer filters to open pull requests, so a merged or closed one stays visible while it is recent and the new colours mean something. The page header still reports the true open count, which comes from `viewer` rather than from the list.
- Fixed the GitHub calendar phase failing outright against the real API. GitHub's GraphQL endpoint delimits a response by closing the connection, sending neither `Content-Length` nor `Transfer-Encoding: chunked` — its REST endpoints do send a length, which is why OTA was unaffected — and `httpReadResponseHeaders` rejected an unframed body before reading a byte of it. Streaming callers can now opt into close-delimited bodies; `JsonScanner` already stopped at end of stream. Callers that need the size in advance, such as the firmware writer, keep the strict behaviour.
- Raised the GitHub response ceiling from 24 KB to 96 KB. The response is parsed as it arrives and never buffered, so the limit bounds how long a reply may hold the single network slot rather than how much RAM it needs, and the old value could reject a busy account's contribution calendar sight unseen.
- Replaced the GitHub screen's catch-all "GITHUB HTTP ERROR" with messages that name the failure: the GraphQL error text where the API supplies one, otherwise the HTTP status. A request that returns no response at all is now distinguished from one that returns an unusable body.
- Fixed the first attempt of a GitHub request not being retried after a connection failure. The retry test covered negative and 5xx codes but not the zero a timeout leaves behind.
- Fixed the pulse page reporting "LOADING" forever after its contribution calendar had actually failed. The calendar phase wrote its failure into the error state shared with the action lists, and the next successful list fetch cleared it, leaving no record of why the page was empty. The calendar now keeps its own failure state, so the page names the API error, points at the `read:user` scope, and counts down to the next attempt, while the shared header rule stays neutral because the lists themselves are fine.
- Added a busy lamp to the GitHub screen for the duration of its blocking TLS refresh. The ESP8266 has one core and the refresh genuinely stops the panel for a few seconds; only the lamp's own rectangle is pushed, so showing it costs nothing near a repaint.
- Fixed long API messages bleeding off both edges of an empty-state card; they are clipped to the card now.
- Fixed the pulse page disappearing instead of explaining itself. It was dropped from the rotation whenever its contribution calendar was unavailable, which is exactly what a token without `read:user` produces: the page silently vanished and the only signal was a red header rule with no text. A selected page now always appears and names the failure.
- Added per-row repository, number, title and queue age. Review requests are ordered ahead of mentions, and mentions ahead of assigned issues.
- Added an explicit `ALL CLEAR` state so an empty inbox reads as an answer rather than a blank panel.
- Arriving from the carousel now always starts at the first selected page rather than mid-rotation.
- A failed refresh keeps the last good snapshot and turns the header rule red instead of blanking the screen.
- Split GitHub polling into two phases over the one GraphQL endpoint. Action lists refresh every cycle; the three-month contribution calendar, which is most of the payload and changes once a day, refreshes hourly as a scheduler continuation. A failed calendar phase no longer backs off the whole screen.
- The POST body is now streamed from PROGMEM in fixed chunks instead of being staged in a 640-byte RAM buffer, and response staging moved off the stack, lowering peak stack use during the TLS session.
- Extended the portal's token verification to exercise the issue/pull-request search the screen depends on, not only the contribution query.

### Radar

- Fixed the radar reporting no data from a healthy endpoint. The CDN in front of adsb.fi answers an HTTP/1.0 request by closing the connection rather than sending Content-Length or Transfer-Encoding, and the client rejected the unframed body before reading it — the same defect as the GitHub calendar phase, from the same shared helper. ArduinoJson already stopped at the end of the document, so the response parses as-is.
- Coloured aircraft by altitude band, the convention every ADS-B display follows: ground, then bands at 5, 10, 20, 30 and 40 thousand feet. Previously every target was painted the same coral with cyan reserved for the nearest, which encoded almost nothing and turned a busy sky into one mass. The nearest target now keeps a ring instead of its own colour, so highlighting it costs no altitude information.
- Added an on-screen altitude key and tied the nearest-target ring to the readout that names it, so neither the colours nor the ring need explaining away from the device.
- Fixed every aircraft being drawn 45 degrees clockwise of its track. Icon packs draw a vehicle at whatever angle suits a toolbar and Lucide's plane flies northeast, which the rotation frames inherited. Packs now carry a `rotation_offset` so the house angle is corrected once per pack, `base=` overrides it for a glyph that disagrees with its own pack, and the generator measures frame 0 and fails the build with the exact correction if it does not point north.
- Replaced the hand-built aircraft silhouette with a rotated icon, in two sizes standing in for emitter-category scaling. Rotorcraft keep their drawn rotor disc, which looks the same from every heading.
- The radar source test now names the failure — unreachable, rate limited, or an unexpected status — rather than reporting every outcome as invalid aircraft data. An empty but valid sky was already a success and stays one.

### Icons

- Added rotation frames: `rot=N` in `assets/icons.overrides` rasterizes N evenly spaced rotations of a glyph, and `gfxDrawIconRotated` picks the nearest. The rotation is applied to the vector before rasterizing, so every frame is as clean as the unrotated one.

### Response parsing

- Added `src/core/JsonScanner.h`, a streaming allocation-free JSON walker that tracks container nesting and array indices.
- Fixed a latent defect in GitHub response handling: the previous reader matched keys at any depth, so any `message` field in a successful payload was reported as a GraphQL error. Errors are now matched only inside the top-level `errors` array.
- Identically named fields in different result lists can no longer overwrite one another, which is what made multiple lists possible at all.

### Icons

- Replaced hand-drawn icon primitives with glyphs resolved from real icon packs, and removed the hand-maintained icon list entirely. Firmware code references `Icon::CodeMerge`; `scripts/gen_icons.py` scans `src/` for those identifiers, resolves each against committed pack catalogs, vendors any missing SVG and emits exactly the glyphs in use. An icon that stops being referenced stops being compiled in — the first run of this dropped two glyphs that had been declared but never drawn.
- Indexed Octicons, FontAwesome Free 7.3.1 and Lucide: 4,184 icons searchable offline with `scripts/gen_icons.py --search`, so the available set is whatever the packs ship rather than whatever someone remembered to declare. Every catalog stays searchable regardless of the resolution order, so an icon outside it can be found and then pinned.
- Screens now draw from Lucide alone: one ISC-licensed set with no attribution obligation and one stroke weight throughout. Lucide ships no fills, so status badges are rings at 14px with a lowered alpha threshold rather than discs at 12px; it ships no brand marks, so the loading screen uses `git-graph`; and it has no `code-review`, so a review request uses `message-square-code`.
- Added `--from DIR` to both icon scripts, so an unpacked official FontAwesome download can replace the network for both indexing and vendoring. The location is remembered in a gitignored `assets/icons/sources.conf`, and a machine without the package falls back to downloading.
- FontAwesome now resolves against `svgs-full/`, whose drawings are all normalized to a square 640x640 box; its default `svgs/` are tight-cropped and non-square for 1762 of 2883 icons.
- An identifier is the pack's own name in PascalCase, with a trailing number for the render size, so one drawing serves several sizes without a declaration for each. Sizes now resolve to a pack's purpose-drawn variant where one exists, replacing downscaled 16-pixel art for the 12- and 24-pixel badges.
- Added `gfxDrawIcon` / `gfxDrawIconCentered` in `src/display/Icons.h`. Glyphs are 1-bit masks in flash, so they need no RAM copy and composite over whatever is behind them.
- `assets/icons.overrides` is now the only hand-edited icon file, and only for pinning a pack or tuning a threshold.
- A glyph whose source is not square is centered in its box rather than stretched to fill it, so FontAwesome's 512-tall variable-width drawings keep their proportions.
- Ordinary firmware builds need neither Python nor network access. CI verifies the generated sources still match the icons `src/` references.

### Emulator and verification

- Added a JSON scanner test covering list isolation, arrays nested under a reused name, error scoping, depth overflow, truncation and string escapes.
- Added GitHub screen coverage to the emulator suite: the three pages render from recorded responses, the contribution heatmap is asserted by pixel, and a token-less run is required to differ.
- Emulator fixtures are now selected after the request body is written, so the two GitHub polling phases can be answered from separate recordings.

## 4.7.3 — 2026-08-09

### Portal and configuration

- Restored airport editing and persistence in the radar configuration page.
- Added a read-only LittleFS inspector with file listing, viewing and download.
- Added configuration versioning and migration for legacy setup data.
- Added no-store headers so setup pages and configuration responses do not remain stale in browser caches.
- Standardized product and emulator identity on DeskMate.

### Display

- Added Wi-Fi RSSI in dBm with signal bars on the Network Guardian screen.
- Dynamically positioned the latency unit after the measured value.
- Moved the weather scene six pixels upward while keeping telemetry and forecast layout unchanged.

### Emulator and verification

- Added virtual LittleFS enumeration and URL query decoding to the emulator.
- Added recorded weather responses and portal integration checks covering migration, airport persistence, filesystem access and traversal rejection.

## 4.7.2 — 2026-08-09

### Network and recovery reliability

- Restored the provider-owned HTTP/TLS clients for Weather, Radar, GitHub, and
  OTA metadata while keeping shared networking responsible for bounded status
  and response-size validation.
- Applied finite configured timeouts to provider requests, stream parsing,
  network probes, release checks, and OTA firmware downloads.
- Added a plain-text `/crashlog` route with reset reason, crash flag, exception
  PC, fault address, and the raw ESP8266 reset record.
- Added a minimal `/update` upload form that remains available in setup and
  crash-recovery modes.
- Setup/fallback AP mode now advertises `DeskMate-Setup` and migrates the old
  legacy setup AP value for unconfigured devices.

### Display and preview

- Unified boot, setup, message, crash, and OTA rendering between firmware and
  preview; removed unused top accent/status bars and reflowed the 240 x 240
  panels.
- Weather typography selects one worst-case black/white contrast style for
  each background region instead of mixing text colors across a tile.
- Increased rain-particle motion and redraw cadence.
- Anchored the newest Network Guardian sample at the graph's right edge while
  short histories grow leftward.
- The preview opens at physical 240 x 240 size, supports free window resizing,
  rebuilds on firmware-source changes, and covers boot, setup, recovery, OTA,
  radar trails, and error fixtures.

### Radar trails and heading

- Reduced trails to 16 compressed points, records movement only after 2 km,
  and removes history beyond 100 km instead of storing every five-second poll.
- Replaced straight trail segments with short faded curves.
- Restored the ADS-B ground-track heading separately from the aircraft's
  position bearing, without restoring the removed speed/vector fields.

## 4.7.1 — 2026-08-08

- Centralized bounded HTTP/1.0 GET handling for Weather, Radar, and OTA release checks.
- Removed per-request `HTTPClient` URL and header `String` allocations.
- Added fixed response-header validation with timeout, content-length limits, and rejection of unknown/chunked bodies.
- Verified all 39 preview fixtures and produced the ESP8266 OTA binary successfully.

## 4.6.0 — Decoupled Trail Pool, Vector Line Trails, and TLS Optimizations

- Decoupled flight trails from the `Aircraft` struct into a relative-offset compressed static pool, supporting up to 30 aircraft with 30-point trails while saving RAM.
- Replaced the discrete trail dots with continuous fading vector line segments (`drawLine`) mimicking an ATC radar scope.
- Added a "Flight trails" presentation checkbox to the Web UI settings card to toggle trails on/off.
- Implemented BearSSL TLS Session Resumption for radar and weather clients, reducing subsequent connection latency from 3+ seconds to ~0.3 seconds.
- Bypassed blocking Maximum Fragment Length Negotiation (MFLN) probing, eliminating 4 seconds of connection latency.
- Increased radar test timeout budget to 8000 ms to handle first-run BearSSL handshakes safely.
- Cleaned up status indicators, removing the custom GitHub screen status dot to ensure exclusive and uniform `StatusDot` presentation on the Network and Flight Radar screens.

## 4.5.1 — Removed gradients from webUI

## 4.5.0 — Scenic Weather renderer

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

## 4.4.0 — Native 240 × 240 desktop preview

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

### 4.3.6

- Pixel-aligned GitHub stats panel and compact, independently aligned range/total/streak labels.
- Added a readable blended telemetry plate behind Weather feels-like/humidity/wind data.
- Replaced status pulse rings with a simple color-state double-heartbeat LED.
- Visible Network/Radar pages show a solid blue LED before synchronous network work, then resume the state-colored heartbeat.
- Tiny status updates now push only an 11x11 retained region instead of a full 40x40 tile.
- Manual OTA uploads start with an empty left-anchored 0% progress bar; indeterminate center fill is reserved for unknown-length downloads.

## 4.3.2 — GitHub heatmap geometry fix

- Contribution cells now use the same width and height.
- Cell size is chosen from both graph width and graph height.
- 1, 3, 6, and 12-month grids are centered in a larger calculated graph region.
- Short ranges no longer render as horizontal strips.

### Scalable polling architecture

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

### Browser-assisted validation

- Location search and timezone resolution now happen in the browser through Open-Meteo.
- The browser verifies OpenWeather current and forecast responses before changed weather configuration can be saved.
- The device stores canonical city, country, latitude, longitude, IANA timezone, abbreviation and current UTC offset; normal device weather calls use only coordinates.
- The browser verifies the GitHub token/account and GraphQL contribution access.
- GitHub contribution range can be selected as 1, 3, 6 or 12 months.
- Network and radar settings use bounded one-time device test endpoints when browser CORS/raw TCP limitations prevent direct verification.
- The device repeats cheap structural checks and rejects invalid configuration with HTTP 422.
- At least one carousel feature is always required both in the browser and device settings.

### Reliability and rendering

- Weather and GitHub retain their last successful snapshots through transient failures.
- Weather current and forecast calls are split into scheduler phases so one feature cannot own two back-to-back provider requests as a single opaque task.
- Radar remains static between data updates; the scan animation is removed.
- Boot, setup and recovery screens use measured text, explicit safe regions and compile-time layout assertions for the 240 × 240 panel.
- Browser requests have explicit abort timeouts.
- GitHub's 12-month range is bounded to 365 days.

### 4.3.1

- Replaced the unsupported three-argument ESP8266 `WiFiClient::connect` calls.
- Added `platformTcpConnect()`, which applies `setTimeout()` before using the core's supported two-argument connect API.
- Removed the `clockTimeStr()` format-truncation warning by using bounded `strftime()`.
