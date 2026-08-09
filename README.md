# DeskMate

DeskMate is a custom 240 × 240 desk-dashboard firmware for ESP8266/ESP32 ST7789 display devices such as the SD Pro and compatible DeskMate hardware.

## DeskMate 4.7.3

DeskMate 4.7.3 uses a compact palette-indexed alpine scene traced from the supplied reference animation: its mountain contours, lake, forest edge, foreground framing, and tent retain the source composition while runtime colors remain weather- and time-driven. Moving cloud lanes, reference-matched sun and moon paths, sun flare, twinkling stars, and rain/snow motion preserve live conditions and the day/night cycle. Dawn and dusk are transition ranges rather than hard theme switches.

Weather telemetry is integrated into one continuous forecast card, leaving the lake and tent unobstructed. The card automatically changes between a light and dark glass tint while keeping one uniform substrate, so lake highlights cannot turn telemetry into a separate strip. The complete UI redraws only when data or the minute changes; scenic animation recomposes only the upper 157 rows every 2.5 seconds, leaving the forecast panel retained in LCD RAM.

The native Linux emulator runs the same application lifecycle and provider clients as the ESP firmware against desktop implementations of display, networking, storage, clock, web-server, and OTA resources. Shared firmware edits therefore appear without maintaining a second preview behavior system.

Version 4.3 separates **data acquisition** from **display rendering**. Every screen selected in the carousel keeps an independent refresh schedule while hidden, but only the visible screen renders. A central cooperative scheduler owns all polling, permits one network-heavy job at a time, and keeps the latest cached snapshot for instant carousel transitions.

### Rendering and polling architecture

The GitHub heatmap now calculates one shared cell dimension from both the available width and height. Every day remains a strict square for 1, 3, 6, and 12-month ranges; shorter grids are enlarged and centered instead of being stretched into horizontal strips.

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
- **Aircraft radar** — Static full-screen PPI scope with airports, vectors, labels, range rings and aircraft silhouettes scaled from ADS-B emitter category when the feed provides it.
- **GitHub activity** — Authenticated-user commits, open issues, open pull requests, streak and a contribution graph. The graph range is configurable as 1, 3, 6 or 12 months.
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
