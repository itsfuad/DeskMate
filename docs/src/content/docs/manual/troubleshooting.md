---
title: Troubleshooting
description: Fixes for the problems a non-technical user is most likely to run into.
---

Common problems and what to do about them, in the everyday terms someone using the device day to day would reach for. For flashing and recovery problems (the device stuck after installing the firmware, or reverting to the original software), see [Flashing](/smalltv-mod/getting-started/flashing/) and [Recovery and credits](/smalltv-mod/reference/recovery/) instead.

## I can't find the settings page anymore

Unplug the device and plug it back in. For a few seconds after it reconnects, the screen shows either:

- a green **CONNECTED** screen with its IP address and a `.local` name address, if it already knows a WiFi network, or
- a yellow **SETUP MODE** screen with a hotspot name and `http://192.168.4.1`, if it does not (see [Quick start](/smalltv-mod/manual/quick-start/))

Whichever screen you get tells you exactly where to browse. Your phone or computer needs to be on the same WiFi network as the device; this does not work over mobile data.

If the `.local` address does not open in your browser, try the plain IP address instead. Some networks and some browsers do not support `.local` names well.

## My WiFi network is not in the scan list

- The device only sees **2.4 GHz** networks. If your router shows separate `_2.4G` and `_5G` names, connect it to the 2.4 GHz one. If it shows one combined name for both bands, most routers still let 2.4 GHz devices join through it, but a few do not; check your router's WiFi settings for a way to split the bands temporarily.
- Move the device closer to the router for the scan, then move it back afterwards if needed; a saved network with a weak signal still works, but it needs to be visible to appear in the scan list at all.
- Hidden networks (ones that do not broadcast their name) never appear in a scan. Skip scanning and type the name directly into a WiFi row before entering the password.
- Press **Scan networks** again; a first scan sometimes misses a network that a second one catches.

## A symbol shows "fetch error" or stays on "loading"

- Double check the spelling. Yahoo Finance symbols are case-sensitive-looking but usually written in capitals: `AAPL`, not `apple`. Swiss and European stocks need their exchange suffix, `NESN.SW` not `NESN`.
- A brand-new symbol takes a few seconds to resolve after you save it; give it one full refresh cycle before assuming it is wrong.
- Press **Refresh data now** in the Status tab to force an immediate retry rather than waiting for the next scheduled poll.
- If it is a cash.ch source symbol, it needs the exact listing key format, not a plain ticker; use the **cash.ch symbol finder** in the Ticker tab to generate it rather than typing one by hand.
- If every symbol shows the error at once, the device likely lost its connection; check the Status tab for whether it still shows as connected, and see the WiFi section below.

## The device shows the wrong time, or night mode isn't dimming the screen

The device gets the time from the internet and only checks it while night mode is turned on. If night mode is off, the clock line in the Display tab says as much and nothing about this affects the ticker or radar.

- Give it a minute after saving: the first check happens shortly after you enable night mode, not instantly.
- Confirm the timezone in the Clock & night mode card matches where you are; a correct clock in the wrong timezone will dim at the wrong local time.
- If the device cannot reach the internet at all, it leaves the screen on rather than guess, and keeps retrying until it succeeds or the night window ends for the day. This is intentional: it never gets stuck dark.
- Right after a restart, the screen may show full brightness for a few seconds inside the night window, until the next time check lands.

## A ticker stopped working after I turned on night mode

This is specific to the original ESP8266 model with cash.ch tickers. Night mode's clock check and a cash.ch fetch both need a chunk of the device's limited memory at the same time, and on this older chip that can be too much at once, so the cash.ch ticker starts failing. Two fixes, either one works:

- switch that ticker's source from **cash.ch** to **GitHub** in the Ticker tab; it fetches the same instrument through a route that costs less memory, or
- turn night mode off on that particular device

The newer ESP32-based models have more memory and are not affected by this.

## I forgot my WiFi password and need to move the device

You do not need the old password to add a new network. Open the WiFi tab, scan, tap the new network, enter its password, and save; up to four networks can be saved at once, so the old one does not need to be removed first.

If the device cannot reach any saved network at all (say, after a full house move), it falls back to its own SETUP MODE hotspot automatically, and you start over from [Quick start](/smalltv-mod/manual/quick-start/).

## I pressed Factory reset and lost my settings

Factory reset is meant to erase everything, so this is expected: it wipes saved WiFi networks, tickers, and every other setting, and restarts the device in first-time SETUP MODE. It does not touch the firmware itself, so no reinstall is needed. Set the device up again as in [Quick start](/smalltv-mod/manual/quick-start/).

If you exported a settings backup beforehand (Update tab, "Export settings"), importing that file restores everything at once instead of re-entering it by hand.

## The screen stays dark even though the device is powered

- Check brightness in the Display tab is above 0, and that a night-mode window is not currently active with its night brightness set to 0 (which turns the backlight fully off on purpose).
- If the backlight circuit is inverted from what the firmware expects, the screen can appear dark even at full brightness; toggle "Backlight is active-low" in the Display tab.
- If the device does not respond to the settings page at all, it may not be reachable on the network; see "I can't find the settings page anymore" above.

## The screen shows CRASH

Rare, but if it happens: the device restarted after an internal fault, and stays on this screen instead of trying to redraw whatever crashed it. It shows a fault code and its IP address, and the settings page and firmware update are still reachable at that address. Reinstalling the latest firmware version through the Update tab normally clears it, and no settings are lost by doing so.

## Updating from GitHub fails

- On the original ESP8266 model, firmware version 2.6.1 and earlier cannot update themselves due to a bug in that version's updater; a broken updater cannot fix itself. Update that device once by hand as described in [Flashing](/smalltv-mod/getting-started/flashing/#after-the-first-flash), and the automatic updater works normally afterwards.
- A failed update leaves the device running its current version; nothing is lost, and the Update tab shows the reason for the failure. Usually pressing "Check for latest" and "Update now" again succeeds on a retry.
- The download needs the device's normal internet connection; if WiFi is unstable at the time, wait and try again.

## Something else

Open an issue on the project's [GitHub page](https://github.com/giovi321/smalltv-mod/issues) with your board type (from the chip badge in the settings page header), the firmware version (Status tab), and what you tried.
