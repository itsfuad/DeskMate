# DeskMate

DeskMate is a custom 240 × 240 desk-dashboard firmware for ESP8266/ESP32 ST7789 display devices such as the SD Pro and compatible DeskMate hardware.

## DeskMate 4.7.3

DeskMate 4.7.3 uses a compact palette-indexed alpine scene traced from the supplied reference animation: its mountain contours, lake, forest edge, foreground framing, and tent retain the source composition while runtime colors remain weather- and time-driven. Moving cloud lanes, reference-matched sun and moon paths, sun flare, twinkling stars, and rain/snow motion preserve live conditions and the day/night cycle. Dawn and dusk are transition ranges rather than hard theme switches.

Weather telemetry is integrated into one continuous forecast card, leaving the lake and tent unobstructed. The card automatically changes between a light and dark glass tint while keeping one uniform substrate, so lake highlights cannot turn telemetry into a separate strip. The complete UI redraws only when data or the minute changes; scenic animation recomposes only the upper 157 rows every 2.5 seconds, leaving the forecast panel retained in LCD RAM.

The native Linux emulator runs the same application lifecycle and provider clients as the ESP firmware against desktop implementations of display, networking, storage, clock, web-server, and OTA resources. Shared firmware edits therefore appear without maintaining a second preview behavior system.

Version 4.3 separates **data acquisition** from **display rendering**. Every screen selected in the carousel keeps an independent refresh schedule while hidden, but only the visible screen renders. A central cooperative scheduler owns all polling, permits one network-heavy job at a time, and keeps the latest cached snapshot for instant carousel transitions.

### Rendering and polling architecture

The GitHub heatmap calculates one shared cell dimension from both the available width and height, so every day stays a strict square and short grids are enlarged and centered instead of stretched into horizontal strips.

The GitHub screen splits its polling across two phases against the one GraphQL endpoint. The action lists are small and refresh on every cycle; the three-month contribution calendar is most of the payload and changes once a day, so it refreshes hourly and is requested as a scheduler continuation. Responses are read by a streaming, allocation-free JSON walker (`src/core/JsonScanner.h`) that tracks container nesting, so identically named fields in different lists cannot be conflated and a `message` field in the payload cannot be mistaken for a GraphQL error.

When demand exceeds the ESP8266's capacity, DeskMate degrades predictably instead of accumulating work:

- missed deadlines are coalesced into one latest-refresh obligation;
- visible and upcoming screens receive priority;
- slow providers consume a shared network-duty budget;
- low-priority work is deferred under load;
- failures use exponential backoff;
- previous successful data remains visible;
- scheduler load, coalescing and deferrals are exposed in the web status page.

## Views

- **Weather** — OpenWeather current conditions and four upcoming 3-hour forecast points. The reference-traced scene includes an alpine valley, layered mountains, lake reflections, pine forests, a foreground tent, moving clouds, precipitation, sun and moon arcs, optical-axis flare artifacts, and twinkling stars. A dedicated condition marker remains visible even in clear weather. Two-tone pixel typography reverses its face/shadow contrast between day and night. Morning, noon, afternoon, evening, and night blend continuously through explicit dawn/dusk transition ranges. Weather conditions tint the same time-driven scene instead of replacing it. Telemetry shares the unified forecast card so the lake remains unobstructed. The browser resolves a city through Open-Meteo, verifies the OpenWeather key, and sends canonical coordinates/timezone data to DeskMate.
- **Network guardian** — TCP latency, DNS timing, availability, outage history, Wi-Fi quality and local IP.
- **Aircraft radar** — Static full-screen PPI scope with airports, vectors, labels and range rings. Targets are drawn as heading-rotated aircraft icons in two sizes taken from ADS-B emitter category, coloured by altitude band the way ADS-B displays conventionally are — ground, then 5, 10, 20, 30 and 40 thousand feet. The nearest target is ringed rather than recoloured, so the highlight costs no altitude information, and the same ring marks the readout that names it. An altitude key sits in the top-left corner.
- **GitHub activity** — Answers "is something waiting on me?" before "how much have I done?". Up to three pages rotate:
  - **Inbox** — review requests, mentions and assigned issues as rows carrying repository, number, title and queue age, review requests first. An empty inbox reads `ALL CLEAR` rather than a blank panel.
  - **My pull requests** — your recent PRs, each with two state badges: the review decision (approved / changes requested / awaiting review) and the CI check rollup (passing / failing / pending). Approved and green means it is ready to merge; a red check means a build broke. Rows are coloured by GitHub's own state palette — open green, draft grey, merged purple, closed red — with the glyph matching the state.
  - **Pulse** — open issues, open pull requests, period commits, contribution total, day streak and the three-month contribution heatmap.

  Which pages take part is selectable in the web UI, and the screen's share of display time is divided between the selected ones — a 12-second carousel dwell with all three selected gives each 4 seconds, so one visit covers the rotation exactly once. Arriving from the carousel always starts at the first selected page. A selected page always appears: if the contribution calendar is unavailable — a token without `read:user` cannot read it at all — the pulse page says so rather than dropping out of the rotation. A failed refresh keeps the last good snapshot on screen and turns the header rule red instead of blanking the page.
- **Carousel** — Rotates through any selected views while all selected data sources continue their background schedules.

All active views use a static 40 × 40 RGB565 tile backbuffer. The visible LCD is never cleared before a replacement tile is fully composed.

## Browser-assisted configuration

Open the device IP address or `http://deskmate-xxxx.local`.

The browser validates critical user input before saving:

- searches and resolves the weather location;
- resolves the current timezone offset;
- verifies the OpenWeather current/forecast calls;
- verifies the GitHub token and contribution access;
- tests network and radar targets through bounded one-time device test endpoints when a browser cannot perform the protocol directly;
- validates ranges, ports, intervals, hostnames, schedules and firmware files.

DeskMate repeats inexpensive structural validation on the device. Secrets are write-only from the portal's perspective and are not returned by the configuration API.

## Time

NTP supplies UTC. The browser-resolved location provides an IANA timezone label and current UTC offset, while normal OpenWeather polling refreshes the offset. The cached offset is available immediately after reboot for the clock, forecast timestamps and scheduled night brightness.


## Desktop emulator

On Fedora, install the native build dependencies:

```bash
sudo dnf install gcc-c++ cmake libX11-devel openssl-devel
```

Launch the full firmware application using any supported board profile:

```bash
./emulator/run.sh --board esp8266
./emulator/run.sh --board esp32c2
./emulator/run.sh --board esp32
```

The X11 window shows the real 240 × 240 RGB565 framebuffer and the real web portal is served at `http://127.0.0.1:8080`. API calls use the PC network; configuration and virtual flash persist per board.

Run headlessly or rebuild automatically while editing:

```bash
./emulator/run.sh --headless --duration-ms 1000 --output deskmate.bmp
./emulator/run.sh --watch --board esp8266
```

No feature state is mocked. Deterministic tests replay recorded raw provider responses through the real clients. See [`emulator/README.md`](emulator/README.md) for resource controls, OTA behavior, watch mode, and verification.

## Build

```bash
pio run -e deskmate
```

Firmware output:

```text
.pio/build/deskmate/firmware.bin
```

Upload the binary from the DeskMate System tab, or over UART:

```bash
pio run -e deskmate -t upload --upload-port /dev/ttyUSB0
```

## Icons

UI glyphs come from real icon packs rather than hand-drawn primitives, and
there is no list of icons to maintain. Reference one in firmware code:

```cpp
gfxDrawIcon(canvas, Icon::CodeMerge, x, y, color);
```

then regenerate. `scripts/gen_icons.py` scans `src/` for `Icon::` identifiers,
resolves each against the committed pack catalogs, vendors any SVG it does not
have yet, and emits exactly those glyphs into `src/display/IconData.{h,cpp}`.
An icon that stops being referenced stops being compiled in.

```bash
python3 scripts/gen_icons.py --search "pull request"   # find a name, offline
python3 scripts/gen_icons.py                           # regenerate from usage
python3 scripts/gen_icons.py --list                    # what is compiled in
python3 scripts/gen_icons.py --preview                 # ASCII proof sheet
python3 scripts/gen_icons.py --check                   # fail if stale
```

`Icon::Name` is the pack's own name in PascalCase; a trailing number is the
render size, default 16. `Icon::DotFill12` and `Icon::DotFill24` are the same
drawing at two sizes, and neither needs declaring.

Octicons, FontAwesome Free 7.3.1 and Lucide are indexed — **4,184 icons**,
searchable without network access. `assets/icons.overrides` exists only to pin a pack when
several ship the same name, or to tune a glyph's threshold. See
[`assets/icons/LICENSES.md`](assets/icons/LICENSES.md) for the licensing and
for why Octicons resolves first.

```bash
python3 scripts/index_icons.py                     # refresh over the network
python3 scripts/index_icons.py --from DIR fontawesome  # from a local package
```

Pointing `--from` at an unpacked official FontAwesome download once removes the
network from the loop entirely: the location is remembered and later runs vendor
glyphs straight out of it.

Ordinary firmware builds need neither Python nor network access because the
generated sources are committed; CI verifies they still match what `src/` uses.

## Other targets

```bash
pio run -e deskmate_c2
pio run -e deskmate_esp32
pio run -e deskmate_loader
```

## Data sources

- Location resolution/timezone: Open-Meteo Geocoding and Forecast APIs
- Weather: OpenWeather Current Weather and 5 Day / 3 Hour Forecast APIs
- Radar: adsb.fi open-data API or a configurable webhook/proxy
- GitHub: GitHub GraphQL API using the configured user token
