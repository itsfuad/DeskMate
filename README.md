# smalltv-mod

Custom open-source firmware for the **GeekMagic SmallTV** (the cheap ESP‑12F /
ESP8266 version with a 1.54" 240×240 ST7789 display). It turns the little display
into a **stock / crypto ticker**: it shows the current price, the change and
%‑change, and a tiny sparkline chart, rotating through several symbols. Out of
the box it pulls data **directly from Yahoo Finance** — no server required — or
you can point it at **your own webhook** (n8n, Node‑RED, anything). Everything is
configured from a built‑in **web UI** — WiFi, what to show, the symbol list, and
**OTA firmware updates**.

It can also flip into a **Claude usage meter**: an animated pixel‑art mascot plus
your **5‑hour and 7‑day Claude usage** bars, fed over WiFi by the
**[clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon)** running on
your PC. Pick the mode in the web UI.

Or a **live plane radar**: a scope centred on your location showing nearby
aircraft (heading, speed vector, callsign, altitude) pulled from the free
[adsb.fi](https://adsb.fi) API. No API key, no receiver, nothing running on a PC.
One firmware carries all three modes; you switch between them in the web UI.

> Not affiliated with GeekMagic or Anthropic. This replaces the stock firmware entirely.

![SmallTV running its three modes: stock ticker, Claude usage, and plane radar](docs/screen.svg)

## Features

- **Stock / crypto ticker** — price, absolute change, %‑change with up/down
  arrow, and a sparkline chart.
- **Multiple symbols** that rotate on a configurable interval.
- **Built-in Yahoo Finance** — fetches quotes & charts directly over HTTPS,
  **no backend needed**. Stocks, ETFs, Swiss equities (`NESN.SW`), crypto
  (`BTC-USD`) and FX (`EURUSD=X`) all work.
- **Or your own webhook** (n8n, Node‑RED, anything) if you'd rather own the
  data source — the device just renders the small JSON contract it's given.
- **Claude usage mode** — an animated pixel mascot + 5h/7d usage bars with reset
  countdowns, fed by the [clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon)
  on your PC (no cable; pushed or polled over WiFi).
- **Plane radar mode** puts nearby aircraft on a radar scope: heading triangles
  with speed vectors, callsign and altitude, and rim dots for traffic outside the
  ring, from the free [adsb.fi](https://adsb.fi) API or your own LAN webhook, plus
  a few home airports you set. Marker size, an altitude filter, and label
  decluttering are all configurable.
- **Full web UI** — connect to WiFi, configure the AP/hotspot, pick what to
  show, manage the symbol list, set brightness/orientation/colours.
- **Updates from GitHub**: the device pulls and flashes the newest release itself
  from the Update tab, or you upload a firmware.bin from the browser. No cable
  needed.
- **Captive-portal setup** — first boot creates a `SmallTV-Setup` hotspot.
- Tiny footprint: ~41 KB free heap, framebuffer-less rendering, HTTP **or**
  HTTPS.

## Hardware

![GeekMagic SmallTV: 45 x 35 x 40 mm body, 28 x 28 mm screen](docs/device.png)

The GeekMagic SmallTV is a tiny desk gadget: a 45 x 35 x 40 mm cube with a 28 x 28 mm
colour IPS screen, an ESP8266 inside, and a USB-C port for power. It usually sells
for about 6 to 8 EUR on AliExpress (search "GeekMagic SmallTV"), which is the whole
reason it is worth flashing. You get a WiFi colour display for well under 10 EUR,
and you never have to open it: the stock firmware exposes an OTA updater, so the
first flash and every one after happen over WiFi.

Check the variant before buying. This firmware targets the cheap ESP-12F / ESP8266
model with the 1.54" 240x240 ST7789 screen. GeekMagic also sells ESP32-S3 and
round-screen versions that this build does not cover.

| | |
|---|---|
| MCU | ESP‑12F (ESP8266, 4 MB flash) |
| Display | 1.54" 240×240 IPS (≈28×28 mm), **ST7789**, SPI |
| Body | 45 × 35 × 40 mm, USB‑C power |
| Backlight | PWM, GPIO5 |
| Light sensor | optional LDR on `A0` (not populated on all units) |
| Price | about 6 to 8 EUR on AliExpress |

### Pin map

These are the pins this firmware drives (confirmed from teardowns and the
ESPHome/Tasmota community — see [Credits](#credits-and-references)):

| Signal | GPIO | Note |
|--------|------|------|
| SPI CLK | 14 | hardware SPI (fixed) |
| SPI MOSI | 13 | hardware SPI (fixed) |
| DC | 0 | boot-strap pin |
| RST | 2 | boot-strap pin |
| CS | 15 | boot-strap pin |
| Backlight | 5 | PWM |

If your unit's screen stays **dark**, try enabling *“Backlight is active‑low”* in
**Display**. If colours/orientation look wrong, change **Orientation** or the
colour scheme. The pins themselves are not configurable from the UI (they're
fixed to the SmallTV wiring); change them in [`src/config.h`](src/config.h) if you
have a different board.

## Get the firmware

You don't need a toolchain — **GitHub Actions builds `firmware.bin` for you**:

- Every push: **Actions** tab → latest `build` run → download the
  `smalltv-mod-firmware` artifact.
- Tagged releases (`vX.Y.Z`): attached to the [Releases](../../releases) page.

Or [build it yourself](#building-from-source).

## Flashing

### Method A — over the air, from the stock web UI (recommended, no soldering)

The stock GeekMagic firmware exposes an OTA updater at `/update` that accepts any
valid ESP8266 image. So you can install this firmware **without opening the
device**:

1. Find the device's IP (it's shown on screen / in the stock Settings app).
2. Browse to `http://<device-ip>/update`.
3. Upload `smalltv-mod-firmware.bin`. It reboots into this firmware.

> **Back up the stock firmware first if you want to be able to go back** — the
> stock `.bin` is not redistributed here. See [recovery](#recovery--going-back).
> Flashing custom firmware is at your own risk.

### Method B — UART header (fallback / recovery)

If OTA isn't available or you bricked it, flash over the serial header. You need a
3.3 V USB‑UART adapter. Pull **GPIO0 to GND** while powering on to enter flash
mode, then:

```bash
# back up the original firmware first (4 MB)
esptool.py --port COM5 read_flash 0x0 0x400000 stock-backup.bin

# write this firmware
esptool.py --port COM5 write_flash 0x0 smalltv-mod-firmware.bin
```

Or with PlatformIO (wiring connected): `pio run -t upload`.

## First-time setup

1. On first boot (no WiFi saved) the device shows **SETUP MODE** and creates an
   open WiFi hotspot **`SmallTV-Setup`**.
2. Join it with your phone/PC. A captive portal should pop up; if not, open
   **http://192.168.4.1**.
3. Go to the **WiFi** tab → **Scan** → pick your 2.4 GHz network → enter the
   password → **Save & connect**. The device reboots and joins your network.
4. The device shows the **network it joined and its IP** on screen at boot (and
   the `http://smalltv.local` address). Browse to it.
5. Add a few tickers under **Symbols** (e.g. `AAPL`, `NESN.SW`, `BTC-USD`). The
   default data source is **Yahoo Finance**, so it works immediately — no server
   to set up. (To use your own backend instead, switch **Display → Data source**
   to *Custom webhook* and paste its URL.)

## Web UI

| Tab | What you can do |
|-----|-----------------|
| **Status** | Live device info (mode, IP, signal, heap, uptime) and current ticker values. “Refresh data now” forces a poll. |
| **WiFi** | Scan & join a network; configure the AP name/password and the mDNS hostname. |
| **Display** | **Mode** (Stock ticker or Claude usage) and its URL; brightness (+ optional auto‑brightness), orientation, backlight polarity, colour scheme, rotation/refresh intervals, **data source** (Yahoo Finance or custom webhook), webhook URL, chart timeframe & points, and **what to show** (name, price, change, chart, timeframe label, “updated N s ago”, rotation dots). |
| **Symbols** | Up to **8** tickers. `symbol` is the Yahoo ticker (e.g. `AAPL`, `NESN.SW`, `BTC-USD`, `EURUSD=X`); `name` is an optional friendly label that overrides the source's name. |
| **Update** | OTA firmware upload, reboot, factory reset. |

“**Save settings**” applies most changes live. Changing the WiFi network reboots
the device.

## Data source

Pick one in **Display → Data source**:

### Yahoo Finance (default — no server)

The device fetches Yahoo's public chart endpoint directly over HTTPS, one request
per symbol:

```
GET https://query1.finance.yahoo.com/v8/finance/chart/AAPL?range=1d&interval=5m
```

It parses the price, previous close (for the change/%‑change), currency and
sparkline itself. Use any Yahoo symbol:

| What | Examples |
|------|----------|
| US / global stocks & ETFs | `AAPL`, `MSFT`, `VOO` |
| Swiss / European stocks | `NESN.SW`, `ROG.SW`, `UBSG.SW`, `BMW.DE` |
| Crypto | `BTC-USD`, `ETH-EUR` |
| FX | `EURUSD=X`, `EURCHF=X` |

The **Chart timeframe** (`1d`, `5d`, `1mo`, `3mo`, `6mo`, `ytd`, `1y`, `2y`,
`5y`, `max`) selects the candle interval automatically. Non‑USD currencies show
as their ISO code (e.g. `CHF 79.73`) since the bitmap font has no `€`/`£` glyph.

> No API key, no account, no backend. The only requirement is outbound HTTPS,
> which the device handles with a bounded TLS buffer (Yahoo's records are small).
> Yahoo's endpoint is unofficial and rate‑limited, so keep the refresh interval
> reasonable (the default 120 s is fine for 8 symbols).

### Custom webhook

Prefer to own the data (other providers, caching, secrets)? Switch the source to
**Custom webhook**. The device then calls *your* URL, **one request per symbol**:

```
GET  <webhookUrl>?symbol=AAPL&range=1d&points=48
```

and expects a small JSON object back:

```json
{
  "symbol": "AAPL",
  "name": "Apple",
  "price": 234.56,
  "currency": "$",
  "change": 2.34,
  "changePct": 1.01,
  "spark": [230.1, 231.0, 229.8, 234.56],
  "range": "1D",
  "ok": true
}
```

Only `price` is required. Full contract, field table, and a **ready-to-import
n8n workflow** are in **[`n8n/`](n8n/README.md)**.

Why pull instead of push? The device fetches on its own schedule, so the backend
never needs to know the device's IP, and it keeps working if the IP changes.

> **Why no cash.ch integration?** cash.ch exposes only a GraphQL API behind
> Akamai bot protection (direct requests return `403`), which an ESP8266 can't
> realistically pass. Swiss equities are already covered via Yahoo's `.SW`
> symbols (priced in CHF), so a dedicated cash.ch path would add fragility for no
> gain. Use the custom‑webhook mode if you ever need a bespoke Swiss source.

## Claude usage mode

Switch **Display → Mode** to **Claude usage** and the device stops showing tickers
and instead shows your Claude consumption — the same idea as a desk usage meter,
but on the SmallTV over WiFi (the device's USB is power‑only; nothing is wired).

It renders two states:

- **Stats** (data flowing): a small mascot, your **5‑hour** and **7‑day**
  utilization as big percentages with fill bars (green → amber → red as you
  approach the cap) and **reset countdowns**.
- **Idle animation** (no data): when the daemon stops sending — PC asleep, daemon
  stopped, network down — the screen plays an animated pixel‑art mascot. The
  creature's mood (calm → working → dancing) reflects how fast your session is
  burning. As soon as data arrives again it snaps back to the stats.

### Setup

> The PC-side daemon is its own project: **[clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon)**.
> It reads the OAuth token Claude Code already stored and delivers your usage to
> the device. (It also drives the USB [Clawdmeter](https://github.com/giovi321/clawdmeter-win) — one tool, three transports: serial / HTTP push / HTTP serve.)

1. On the PC that runs Claude Code:

   ```sh
   git clone https://github.com/giovi321/clawdmeter-daemon
   cd clawdmeter-daemon
   pip install -r requirements.txt
   # push to the SmallTV (works even behind Wi-Fi client isolation):
   python clawdmeter_daemon.py --push-to <smalltv-ip-or-hostname>
   # ...or serve and let the device pull:
   python clawdmeter_daemon.py --serve          # http://0.0.0.0:8787/
   ```

   On **Windows** it runs with a **system-tray icon** (the mascot) and can
   auto-start at login — pick the transport from its menu. See the
   [clawdmeter-daemon README](https://github.com/giovi321/clawdmeter-daemon) for
   tray/autostart and a durable login token.

2. In the web UI → **Display → Mode → Claude usage**. For **push**, leave the
   **Usage URL** blank; for **serve/pull**, set it to `http://<that-pc-ip>:8787/`.
   **Save**.

The device only ever receives a few percentages over your LAN; the token never
leaves your machine. The mascot animations are a curated subset of the
[claudepix](https://claudepix.vercel.app) pixel‑art set, re‑rendered on the
ST7789.

## Plane radar mode

Switch **Display → Mode** to **Plane radar** and set it up in the **Radar** tab.
The screen becomes a radar scope centred on your location: range rings, a home
marker in the middle, nearby aircraft as red heading triangles with a speed vector
and a callsign/altitude label, traffic outside the ring as dots on the rim, and
any airports you add as small markers.

Set your **home latitude/longitude** in decimal degrees, a **range** ring
(5/10/15/25/50), **km or mi**, and a **data source** (below).

### Display options

Three controls in the Radar tab tune what shows on screen:

- **Marker & label size** (Small / Medium / Large) scales the triangles, markers
  and text together. Medium is the default; Large reads from across the room,
  Small keeps a busy sky uncluttered.
- **Hide aircraft below (ft)** drops ground traffic and low flights. Set it to
  `500` and planes parked or taxiing at the airport disappear; `0` shows all.
- Callsigns are placed nearest-first, and any that would overlap a label already
  on screen is skipped, so a crowded scope stays legible. The triangle still shows
  even when its label was dropped.

### adsb.fi (default, no server)

The device fetches the free [adsb.fi](https://adsb.fi) API directly over HTTPS,
one request per refresh:

```
GET https://opendata.adsb.fi/api/v3/lat/<lat>/lon/<lon>/dist/<nm>
```

No API key. The device parses the feed itself and keeps the closest 24 aircraft.
The endpoint is rate-limited to about one request a second, so keep the refresh
interval sensible (10 s by default).

> **ESP8266 TLS note.** HTTPS is tight on RAM here. The device probes the server's
> Maximum Fragment Length support so BearSSL can run with a small buffer. If
> adsb.fi ever sends large TLS records without MFLN, the direct fetch can fail in
> busy airspace; switch to the webhook source below if that happens.

### Custom webhook (LAN proxy)

Point a small proxy (n8n, Node-RED, a short script) at adsb.fi, filter it down to
the nearest few aircraft, and return a small JSON over plain HTTP on your LAN. Set
the source to **Custom webhook**, give it the URL, and the device calls:

```
GET  <webhookUrl>?lat=<lat>&lon=<lon>&dist=<km>
```

It expects the same `{"ac":[ ... ]}` shape adsb.fi returns, with these fields per
aircraft: `lat`, `lon`, `track`, `gs`, `flight`, `hex`, `alt_baro`. The proxy does
the filtering and the TLS handshake, which is the most reliable setup on the
ESP8266.

## Building from source

Requires [PlatformIO](https://platformio.org/):

```bash
pio run                      # build  ->  .pio/build/smalltv/firmware.bin
pio run -t upload            # build + flash over UART
pio device monitor           # serial logs @ 115200
```

Project layout:

```
src/                          shared core (device, net, web, settings)
  main.cpp          orchestration: setup/loop + the mode registry
  Mode.h            DisplayMode interface each feature implements
  config.h          pins, limits, feature flags, compile-time defaults
  Settings.*        settings struct (shared + per-feature slices) + persistence
  Net.*             WiFi STA / fallback AP / captive portal / mDNS
  WebPortal.*       web server, REST API, OTA endpoint
  webui.h           the single-page UI (HTML/CSS/JS, served from PROGMEM)
  Gfx.*             shared ST7789 core (Arduino_GFX): device, text, boot screens
  features/
    ticker/         TickerMode + StockClient/StockData
    usage/          UsageMode + UsageClient/UsageData + Mascot + mascot_frames.h
    radar/          RadarMode + RadarClient + RadarData
tools/              extract_mascot.py — regenerates mascot_frames.h from the source
n8n/                webhook contract + importable workflow

Each feature is a self-contained `DisplayMode` (its own fetch, render, dirty
state, and settings slice); `main.cpp` just dispatches to the active one.
```

The PC-side usage daemon lives in its own repo:
[clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon).

Footprint: ~574 KB flash (of 1 MB) and ~51 % RAM at boot — plenty of headroom for
OTA (which needs room for two sketch copies). The mascot frame data (~33 KB) lives
in flash (PROGMEM), not the heap.

## Recovery / going back

- **Re-flash** anything (stock backup or this firmware) over the UART header
  (Method B). Keep a `stock-backup.bin` if you might want the original apps back.
- **Factory reset** (Update tab) wipes saved settings and restarts in SETUP MODE
  — it does *not* change the firmware.

## Notes & limitations

- 2.4 GHz WiFi only (ESP8266). WPA2; for an AP password use ≥ 8 chars or leave it
  blank for an open hotspot.
- The web server is single-threaded; during a data poll the UI may pause briefly.
- HTTPS works but is RAM-tight on the ESP8266 — prefer plain HTTP on your LAN for
  the webhook if you see instability (see the [n8n notes](n8n/README.md)).
- Fonts are the built-in bitmap font (scaled), chosen for reliability and the
  retro look — no external font files needed.

## Credits and references

- GeekMagic SmallTV / SmallTV‑Pro — original product & stock firmware
  ([GeekMagicClock/smalltv-pro](https://github.com/GeekMagicClock/smalltv-pro)).
- Pin mapping & hardware notes from the ESPHome / Tasmota communities:
  - [ViToni/esphome-geekmagic-smalltv](https://github.com/ViToni/esphome-geekmagic-smalltv)
  - [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
  - [Installing ESPHome on GEEKMAGIC SmallTV (HA community)](https://community.home-assistant.io/t/installing-esphome-on-geekmagic-smart-weather-clock-smalltv-pro/618029)
  - [Puddle of Code — My Own GeekMagic SmallTV](https://puddleofcode.com/story/my-own-geekmagic-smalltv/)
- Claude usage mode is a re-implementation of
  [clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter) for this hardware
  (HTTP/WiFi instead of serial); the mascot frames come from
  [claudepix](https://claudepix.vercel.app).
- Libraries: [Arduino_GFX](https://github.com/moononournation/Arduino_GFX),
  [ArduinoJson](https://arduinojson.org/).

## License

[WTFPL](LICENSE) — Do What The F*ck You Want To Public License.
