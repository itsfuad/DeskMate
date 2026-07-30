# DeskMate

DeskMate is a custom 240 × 240 desk-dashboard firmware for ESP8266/ESP32 ST7789 display devices such as the SD Pro and compatible GeekMagic-style hardware.

## Views

- **Weather** — OpenWeather current conditions plus four upcoming 3-hour forecast points, rendered as a static modern scene. Configure the OpenWeather API key, coordinates, label, units and refresh interval from the web UI.
- **Network guardian** — Internet TCP latency, DNS timing, availability, outage history, Wi-Fi quality and local IP.
- **Aircraft radar** — Full-screen PPI radar with a static PPI scope, range rings, airports, vectors, callsign/flight-level labels and aircraft silhouettes scaled from ADS-B emitter category when supplied by the feed. It redraws only when target data changes.
- **GitHub activity** — Authenticated-user current-year commits, open issues and open pull requests, streak, weekly activity and a 52-week contribution graph using GitHub GraphQL.
- **Carousel** — Rotates through any selected views.

All active views use a static 40 × 40 RGB565 tile backbuffer. The visible LCD is never cleared before a replacement tile is fully composed.

## Configure

Open the device IP address or `http://deskmate-xxxx.local`.

Secrets are entered in the web UI and stored only in LittleFS configuration:

- OpenWeather API key
- GitHub personal access token
- Wi-Fi password

Leaving a secret field blank keeps the stored value.

## Build

```bash
pio run -e deskmate
```

Firmware output:

```text
.pio/build/deskmate/firmware.bin
```

Upload the binary from the DeskMate System tab, or over UART with:

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

- Weather: OpenWeather Current Weather and 5 Day / 3 Hour Forecast APIs
- Radar: adsb.fi open-data API or a configurable webhook/proxy
- GitHub: GitHub GraphQL API using the configured user token


## 4.1 reliability changes

- All feature layouts use an 8 px safe display inset.
- GitHub contribution JSON is parsed as a stream to protect ESP8266 heap.
- GitHub issue and pull-request connections use valid pagination arguments.
- Radar is static between data polls, avoiding a visibly frozen sweep.
- Weather shows the next four 3-hour forecast points and gives more space to time.
