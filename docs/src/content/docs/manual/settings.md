---
title: Settings explained
description: Every tab in the settings page, and every option in it, in plain language.
---

The settings page is a single page with tabs across the top: Status, WiFi, Display, Ticker, Usage, Radar, Update. Not every device has all of them: a unit built without the radar feature, for example, has no Radar tab. This page goes through each tab. For the deeper technical reference (data source details, exact request formats) see [Data sources](/smalltv-mod/reference/data-sources/) and each mode's own page.

One button applies everything: **Save settings**, at the very bottom of the page, under every tab. Nothing you type takes effect until you press it.

## Status

Read-only. Shows the firmware version, whether the device is online, its network name, IP address, signal strength, free memory, how long it has been running since the last restart, and why it last restarted. Below that, the current value of every ticker. "Refresh data now" asks the device to fetch fresh data immediately instead of waiting for the next scheduled poll.

Free memory and "last reset" are mostly useful when something is wrong; see [Troubleshooting](/smalltv-mod/manual/troubleshooting/).

## WiFi

**Saved networks**: up to four. Press **Scan networks** to see what is nearby, tap one to fill a row, then type its password. At every start the device joins whichever saved network it can see with the strongest signal, and switches to another saved one if the connection drops. Leaving a password field blank when editing an already-saved network keeps the password it already has, it does not clear it.

Networks must be 2.4 GHz. If you cannot see your network in the scan, see [Troubleshooting](/smalltv-mod/manual/troubleshooting/#my-wifi-network-is-not-in-the-scan-list).

**Device name (hostname)**: the name used in `http://<name>.local`. Every device ships with a unique default like `smalltv-3fa2` so several units can share a network without clashing; rename it to something memorable (`smalltv-kitchen`) if you like. Saving a new name restarts the device.

**Setup hotspot (AP)**: the name and password of the temporary network the device creates when it has no WiFi to join, or cannot reach any of its saved ones. Change these if you want a different setup network name, or leave them as they are; most people never touch this section again after the first setup.

## Display

**Mode**: which of the three features is on screen, or **Carousel** to rotate between the ones you tick below it, staying on each for the number of seconds you set. Each mode is configured in its own tab regardless of whether it is the active one, so you can set up the ticker and the radar ahead of time and only switch to Carousel once both are ready.

**Screen**: brightness as a percentage; "Auto-brightness" if your unit has a light sensor and you want the screen to follow the room instead of a fixed level; orientation, if the device is mounted sideways or upside down; and a fix for a dark screen with the backlight visibly on, "Backlight is active-low", which is on by default and should stay that way unless the screen looks wrong.

**Clock & night mode**: pick your timezone by name (for example `Europe/Zurich`) from the dropdown; daylight saving is applied automatically, you never touch it again. Turn on "Dim or blank the screen on a nightly schedule" to set a from-time, a to-time, and a night brightness, where 0 turns the backlight fully off and anything else just dims it. This needs the internet once, at the times it checks, to know what time it is; it does not need it constantly, and the device stays reachable through the night either way.

## Ticker

**Rotation & data**: how long each symbol stays on screen, how often the device re-checks prices, the chart's timeframe (a day, a month, a year, and so on), how many points the chart draws, and what the "change" and "% change" numbers measure: the whole chart's span (default) or the classic since-yesterday figure. The full explanation of that last one, with an example of when they disagree, is in [Stock and crypto ticker](/smalltv-mod/features/ticker/).

**Color scheme**: swap which colour means up and which means down, for people used to the reverse convention.

**What to show**: tick or untick the name, price, change line, chart, timeframe label, "updated N seconds ago" line, rotation dots, and the portfolio page, independently.

**Tickers**: up to eight rows, each with a symbol, an optional display name, a data source, and an optional quantity and cost that turn it into a position (see [Everyday use](/smalltv-mod/manual/everyday/#the-portfolio-page)). Yahoo Finance is the default source and needs nothing else set up; the other sources and what their symbol field expects are covered in [Data sources](/smalltv-mod/reference/data-sources/).

**cash.ch symbol finder**: paste a link from cash.ch, an ISIN, or an instrument name, press Find, and click a result to add it as a ticker automatically. This runs the search from your own browser, not from the device.

## Usage

One field: the address of the daemon running on your PC, the small program that reads your Claude usage and sends it to the device. Leave it blank if that program is pushing to the device; fill it in with the PC's address if the device should instead pull from it. Full setup for that PC-side program is in [Claude usage meter](/smalltv-mod/features/usage/), a separate download from a separate project.

## Radar

**Home location**: your latitude and longitude, in decimal degrees. This is the point the radar is centred on. Until these are set, the radar screen shows "Set home location" instead of a scope.

**Range & data**: how far out the scope reaches, in kilometres or miles, how often it refreshes, and where it gets aircraft data from: directly from the free adsb.fi service (no setup needed) or from your own webhook. Both are explained in [Plane radar](/smalltv-mod/features/radar/).

**What to show**: the size of the markers and labels, a minimum altitude below which aircraft are hidden (useful for filtering out ground traffic at a nearby airport), and independent toggles for callsign/altitude labels, speed vectors, and the rim dots for traffic beyond the outer ring.

**Airports**: up to six, each an ICAO code (for example `LSZH` for Zurich) with its own latitude and longitude, drawn as small markers on the scope.

## Update

**Update from GitHub**: shows the installed version, and "Check for latest" compares it against the newest release. If a newer one exists, "Update now" downloads and installs it, restarting the device once or twice depending on the model.

**Manual update (OTA)**: upload a firmware file by hand instead, useful if the device cannot reach GitHub or you built the firmware yourself.

**Settings backup**: "Export settings" downloads the device's whole configuration as a file, including saved WiFi passwords in plain text, so store that file the way you would store a password. "Import" applies a previously exported file and restarts the device; useful when replacing a unit or copying one device's setup to another.

**Maintenance**: "Reboot" restarts the device without changing anything. "Factory reset" erases every saved setting, including WiFi networks, and puts the device back into first-time SETUP MODE; it does not remove or downgrade the firmware itself.
