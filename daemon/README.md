# smalltv usage daemon

Serves your **Claude usage** to the SmallTV over your LAN, so the device can show
it in **Claude usage** mode. No cable and no serial port — the SmallTV is a WiFi
device, so it just **pulls** the numbers over HTTP, exactly like it pulls stock
quotes.

```
+------------------+        polls Claude API         +-----------------------+
|   This PC        |  (rate-limit headers, OAuth)    |  api.anthropic.com    |
|  (Claude Code)   | ------------------------------> |                       |
|                  | <------------------------------ +-----------------------+
|  usage daemon    |   5h / 7d utilization + resets
|  HTTP :8787      |
+--------+---------+
         ^   GET http://<pc-ip>:8787/   (every poll)
         |
   +-----+------+
   |  SmallTV   |  Claude usage mode -> mascot + 5h/7d meters
   +------------+
```

## How it works

The daemon reads the OAuth token **Claude Code already stored** on this machine
(`~/.claude/.credentials.json`, or the macOS Keychain), makes one tiny throwaway
API call, and reads the rate-limit response headers
(`anthropic-ratelimit-unified-5h/7d-*`). It caches the result and serves it as a
small JSON object. It refreshes from the API at most once every ~50s, no matter
how often the device polls.

## Run it

```sh
pip install -r requirements.txt
python smalltv_usage_daemon.py         # tray icon + serves on 0.0.0.0:8787
python smalltv_usage_daemon.py --no-tray   # headless console mode
```

Options:

```
--host 0.0.0.0     # interface to bind (default: all)
--port 8787        # TCP port
--interval 60      # seconds between Claude API refreshes
--no-tray          # run in the console instead of the system tray
```

Then in the SmallTV web UI → **Display → Mode → Claude usage**, set **Usage URL**
to `http://<this-pc-ip>:8787/` (find the IP with `ipconfig` / `ip addr`).

### System tray (Windows)

By default the daemon shows a **system-tray icon** — the little mascot — that
turns grey while it's waiting, red if you're not logged in, and full colour once
it's serving data. Hover for live `5h % / 7d %`; right-click for **Refresh now**
and **Quit**. (The tray needs `pystray` + `Pillow`, both in `requirements.txt`;
without them it falls back to console mode.)

- **`start-daemon.bat`** — double-click to start it **now**, silently (no console
  window), with the tray icon. Run `py -m pip install -r requirements.txt` once
  first.
- **`install.bat`** — installs dependencies and registers it to **start
  automatically at every login** (a shortcut in your Startup folder). Uninstall
  by deleting `…\Startup\SmallTVUsageDaemon.lnk`.

> If `where pythonw` shows a `…\WindowsApps\pythonw.exe` entry, that's the
> Microsoft Store alias stub (it opens the Store instead of running Python). Point
> the launcher at your real interpreter first:
> `set SMALLTV_PYTHONW=C:\Python314\pythonw.exe`

On macOS/Linux the tray works too where a tray host is available; otherwise use
`--no-tray` under systemd / launchd / tmux.

## Response contract

`Content-Type: application/json`, one object:

```json
{ "s": 29, "sr": 142, "w": 4, "wr": 9876, "st": "allowed", "ok": true }
```

| field | meaning |
|-------|---------|
| `s`   | 5-hour window utilization (%) |
| `sr`  | minutes until the 5-hour window resets |
| `w`   | 7-day window utilization (%) |
| `wr`  | minutes until the 7-day window resets |
| `st`  | rate-limit status (`allowed`, `allowed_warning`, `rejected`, …) |
| `ok`  | `false` when there's no data (e.g. not logged in) |

When the daemon is unreachable or returns `ok:false`, the device stops getting
fresh data and, after a short timeout, switches from the stats to the idle
**mascot animation** — until data starts flowing again.

## Keep it running

- **Windows:** run **`install.bat`** once — it auto-starts the daemon (with tray
  icon) at every login.
- **Linux/macOS:** a tiny systemd unit / launchd plist, or just run it under
  `tmux` with `--no-tray`.

## Troubleshooting

**Tray says "Token expired … re-login: claude /login" (or "No token") even though
you're using Claude Code.** Your *current* Claude Code session holds a valid token
in memory, but the copy on disk (`~/.claude/.credentials.json`) is expired, and if
its `refreshToken` is empty nothing can renew it headlessly — a fresh `claude`
process just returns `401`. Fix it by re-authenticating so new credentials get
written to disk:

```sh
claude            # then type:  /login     (or /logout then /login)
```

The daemon picks up the fresh token on its next poll (within ~a minute). The token
is good for several hours; while you actively use Claude Code it keeps getting
refreshed, so in normal use you won't hit this again.

## Notes

- LAN-only by design. The token never leaves your machine; only the percentages
  do. If you expose it beyond your LAN, put it behind auth/HTTPS yourself.
- The throwaway API call uses the cheapest model with `max_tokens: 1`. Its cost
  is negligible, but it is a real request against your account.
- This is a clean reimplementation of the HTTP half of
  [clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter); the serial/USB and
  on-device branding have been removed.
