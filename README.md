# DeskMate

DeskMate is a custom 240 × 240 desk-dashboard firmware for ESP8266/ESP32 ST7789 display devices such as the SD Pro and compatible GeekMagic-style hardware.

## DeskMate 4.4.0

Version 4.4 adds a native Linux desktop preview for the fixed 240 × 240 interface. It compiles the real firmware drawing functions against an RGB565 framebuffer, displays them in an enlarged X11 window, and can generate deterministic BMP screenshots for every UI state. This makes pixel-level layout work possible without repeatedly flashing the device.

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

- **Weather** — OpenWeather current conditions and four upcoming 3-hour forecast points. The browser resolves a city through Open-Meteo, verifies the OpenWeather key, and sends canonical coordinates/timezone data to DeskMate.
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


## Desktop preview

On Fedora, install the native build dependencies:

```bash
sudo dnf install gcc-c++ cmake libX11-devel
```

Launch the interactive preview:

```bash
./preview/run.sh
```

Render all screen fixtures without opening a window:

```bash
./preview/run.sh --all preview-output --scale 1
```

The preview uses the same 40 × 40 tile renderer, RGB565 colors, Adafruit GFX drawing code, classic font, and Weather/GitHub/Network/Radar/OTA layout sources as the firmware. Hardware and API inputs are replaced with deterministic fixtures. See [`preview/README.md`](preview/README.md) for controls, available scenarios, watch mode, and headless testing.

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
