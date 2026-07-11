---
title: First-time setup
description: Join WiFi, reach the web UI, and configure what the SmallTV shows.
---

On first boot the device has no WiFi saved, so it starts its own hotspot and waits for you to point it at your network. Everything after that happens in the web UI.

## Join WiFi

1. On first boot the screen shows **SETUP MODE** and creates an open hotspot named `SmallTV-Setup`.
2. Join it from your phone or PC. A captive portal should open on its own. If it does not, browse to `http://192.168.4.1`.
3. Open the **WiFi** tab, press **Scan**, pick your 2.4 GHz network, enter the password, and press **Save and connect**. The device reboots and joins your network. You can save up to 4 networks; at boot the device joins the strongest one it can see.
4. It shows the joined network, its IP, and its `http://<hostname>.local` address on screen at boot. Browse to either one. New devices get a unique default hostname like `smalltv-3fa2` so several SmallTVs can share a network; rename it in the WiFi tab.

The ESP8266 is 2.4 GHz only. For an AP password use at least 8 characters, or leave it blank for an open hotspot.

## Add something to show

Open the **Ticker** tab and add a few tickers, for example `AAPL`, `NESN.SW`, or `BTC-USD`. Each ticker picks its own data source; the default is Yahoo Finance, so prices appear within a few seconds with no server to set up. Swiss instruments Yahoo lacks can use cash.ch, and a custom webhook lets you own the source. See [Data sources](/smalltv-mod/reference/data-sources/) for the full list of what works.

## Web UI reference

The UI is a single page served from the device. Saving applies most changes live; changing the WiFi network reboots.

### Status

Live device info: mode, IP, signal, free heap, uptime, and the current ticker values. "Refresh data now" forces an immediate poll.

### WiFi

Scan and save up to 4 networks; the device joins the strongest visible one at boot and falls over to the others if the connection drops. Also sets the device hostname (its `.local` mDNS name) and the setup hotspot name and password.

### Display

The mode selector (Stock ticker, Claude usage, Plane radar, or Carousel, which rotates through the ticked features on a timer), plus brightness with optional auto-brightness, orientation, and backlight polarity.

### Ticker

Up to 8 tickers, each with its own data source (Yahoo Finance, cash.ch, GitHub, or your webhook). `symbol` follows the source: a Yahoo ticker (`AAPL`, `NESN.SW`, `BTC-USD`, `EURUSD=X`), a cash.ch listing key for cash.ch or GitHub (the built-in finder turns a cash.ch link or ISIN into one), or whatever your webhook expects. `name` is an optional label that overrides the source's own name. Optional `qty` and per-unit `cost` turn a ticker into a position: its page gains a P/L line and a portfolio summary page joins the rotation. The tab also sets the chart timeframe and point count, rotation and refresh intervals, the colour scheme, and which fields to draw.

### Update

Check for and install the newest GitHub release (every board fetches its own image), upload a firmware file manually, export or import the full device configuration as a JSON file, reboot, or factory reset. The exported file contains the WiFi passwords in clear text, so treat it like a password.

## Modes

Each mode has its own page:

- [Stock and crypto ticker](/smalltv-mod/features/ticker/)
- [Claude usage meter](/smalltv-mod/features/usage/)
- [Plane radar](/smalltv-mod/features/radar/)
