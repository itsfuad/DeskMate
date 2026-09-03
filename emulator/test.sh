#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d -t deskmate-emulator-XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT

"$ROOT/emulator/build.sh"
"$ROOT/emulator/build/deskmate-radar-trail-test"
"$ROOT/emulator/build/deskmate-radar-client-test" \
  "$ROOT/emulator/tests/fixtures" "$TEST_DIR/radar-state"
"$ROOT/emulator/build/deskmate-connectivity-test"
"$ROOT/emulator/build/deskmate-json-scanner-test"

for board in esp8266 esp32c2 esp32; do
  image="$TEST_DIR/$board.bmp"
  (cd "$ROOT" && "$ROOT/emulator/build/deskmate-emulator" \
    --headless --duration-ms 350 --board "$board" \
    --state-dir "$TEST_DIR/$board-state" --web-port 0 \
    --output "$image" --scale 1)
  size="$(stat -c '%s' "$image")"
  if [[ "$size" -ne 230454 ]]; then
    echo "Unexpected 240x240 BMP size ($size): $image" >&2
    exit 1
  fi
done

PORTAL_STATE="$TEST_DIR/portal-state"
mkdir -p "$PORTAL_STATE/littlefs"
cp "$ROOT/emulator/tests/fixtures/legacy-config.json" \
  "$PORTAL_STATE/littlefs/config.json"
printf 'LittleFS listing fixture\n' > "$PORTAL_STATE/littlefs/notes.txt"
PORT=$((18080 + $$ % 1000))
PORTAL_LOG="$TEST_DIR/portal.log"
(
  cd "$ROOT"
  "$ROOT/emulator/build/deskmate-emulator" \
    --headless --duration-ms 15000 --board esp8266 --network ap \
    --state-dir "$PORTAL_STATE" --web-port "$PORT" \
    > "$PORTAL_LOG" 2>&1
) &
PORTAL_PID=$!
trap 'kill "$PORTAL_PID" 2>/dev/null || true; wait "$PORTAL_PID" 2>/dev/null || true; rm -rf "$TEST_DIR"' EXIT
BASE="http://127.0.0.1:$PORT"
for attempt in {1..30}; do
  if curl --silent --fail "$BASE/api/status" >/dev/null 2>&1; then break; fi
  sleep 0.1
done

headers="$(curl --silent --show-error --dump-header - --output /dev/null "$BASE/")"
grep -qi '^Cache-Control: no-store' <<<"$headers"
grep -q 'Airports' <(curl --silent --show-error "$BASE/")
grep -q 'LittleFS' <(curl --silent --show-error "$BASE/")
portalPage="$(curl --silent --show-error "$BASE/")"
grep -q 'class="fs-list"' <<<"$portalPage"
grep -q 'Read-only filesystem listing' <<<"$portalPage"
if grep -q 'fs-view\|fs-download' <<<"$portalPage"; then
  echo "LittleFS listing unexpectedly exposes file action links" >&2
  exit 1
fi
config="$(curl --silent --show-error "$BASE/api/config")"
grep -q '"configVersion":1' <<<"$config"
grep -q '"apSsid":"DeskMate-Setup"' <<<"$config"
files="$(curl --silent --show-error "$BASE/api/fs")"
grep -q '"path":"/config.json"' <<<"$files"
grep -q '"path":"/notes.txt"' <<<"$files"
grep -q 'LittleFS listing fixture' <(curl --silent --show-error \
  "$BASE/api/fs/file?path=%2Fnotes.txt")
downloadHeaders="$(curl --silent --show-error --dump-header - --output /dev/null \
  "$BASE/api/fs/file?download=1&path=%2Fnotes.txt")"
grep -qi 'content-disposition: attachment; filename="notes.txt"' <<<"$downloadHeaders"
if curl --silent --show-error --fail "$BASE/api/fs/file?path=%2F..%2Fconfig.json" >/dev/null 2>&1; then
  echo "LittleFS traversal request unexpectedly succeeded" >&2
  exit 1
fi
airportResponse="$(curl --silent --show-error --request POST \
  --header 'Content-Type: application/json' \
  --data '{"mode":"radar","radar":{"lat":23.81,"lon":90.41,"source":"direct","rangeKm":50,"pollSec":10,"unitsMi":false,"showLabels":true,"showRimDots":true,"showTrails":true,"uiScale":1,"minAltFt":0,"airports":[{"icao":"VGHS","lat":23.8433,"lon":90.3978},{"icao":"VGTJ","lat":24.4372,"lon":90.3821}]}}' \
  "$BASE/api/config")"
grep -q '"ok":true' <<<"$airportResponse"
grep -q '"icao":"VGTJ"' <(curl --silent --show-error "$BASE/api/config")

kill "$PORTAL_PID" 2>/dev/null || true
wait "$PORTAL_PID" 2>/dev/null || true
trap 'rm -rf "$TEST_DIR"' EXIT

WEATHER_STATE="$TEST_DIR/weather-state"
mkdir -p "$WEATHER_STATE/littlefs"
cp "$ROOT/emulator/tests/fixtures/legacy-config.json" \
  "$WEATHER_STATE/littlefs/config.json"
WEATHER_IMAGE="$TEST_DIR/weather.bmp"
(cd "$ROOT" && "$ROOT/emulator/build/deskmate-emulator" \
  --headless --duration-ms 2200 --board esp8266 \
  --state-dir "$WEATHER_STATE" --web-port 0 \
  --responses "$ROOT/emulator/tests/fixtures" \
  --output "$WEATHER_IMAGE" --scale 1)
if [[ "$(stat -c '%s' "$WEATHER_IMAGE")" -ne 230454 ]]; then
  echo "Weather fixture did not produce a 240x240 framebuffer" >&2
  exit 1
fi

# --- GitHub screen -------------------------------------------------------
# The screen divides its display window between the selected pages, so with a
# 12 s window and all three selected each page holds for 4 s. Each capture is
# timed to land in the middle of one.
GITHUB_STATE="$TEST_DIR/github-state"
mkdir -p "$GITHUB_STATE/littlefs"
cp "$ROOT/emulator/tests/fixtures/github-config.json" \
  "$GITHUB_STATE/littlefs/config.json"
declare -A GITHUB_PAGE=([inbox]=2500 [pulls]=6000 [pulse]=10000)
for page in inbox pulls pulse; do
  image="$TEST_DIR/github-$page.bmp"
  (cd "$ROOT" && "$ROOT/emulator/build/deskmate-emulator" \
    --headless --duration-ms "${GITHUB_PAGE[$page]}" --board esp8266 \
    --state-dir "$GITHUB_STATE" --web-port 0 \
    --responses "$ROOT/emulator/tests/fixtures" \
    --output "$image" --scale 1)
  if [[ "$(stat -c '%s' "$image")" -ne 230454 ]]; then
    echo "GitHub $page page did not produce a 240x240 framebuffer" >&2
    exit 1
  fi
done

# Only a parsed contribution calendar paints the four heatmap greens; the
# loading and error screens contain none of them.
python3 - "$TEST_DIR/github-pulse.bmp" <<'PYCHECK'
import struct, sys
LEVELS = {(8, 69, 41), (0, 109, 49), (33, 166, 66), (57, 211, 82)}
data = open(sys.argv[1], "rb").read()
offset = struct.unpack_from("<I", data, 10)[0]
step = struct.unpack_from("<H", data, 28)[0] // 8
pixels = data[offset:]
found = sum(1 for i in range(0, len(pixels) - step + 1, step)
            if (pixels[i + 2], pixels[i + 1], pixels[i]) in LEVELS)
if found < 3000:
    sys.exit("contribution heatmap missing: %d level pixels" % found)
print("GitHub pulse page: %d contribution cells painted" % found)
PYCHECK

# Pull-request rows are coloured by GitHub's own state palette. The fixture
# carries one of each state, so all four must reach the framebuffer. Draft
# shares its grey with muted body text, so only the three distinctive ones are
# counted.
python3 - "$TEST_DIR/github-pulls.bmp" <<'PYSTATES'
import struct, sys, collections

def expand(r, g, b):
    r5, g6, b5 = (r & 0xF8) >> 3, (g & 0xFC) >> 2, (b & 0xF8) >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))

WANT = {"open": expand(63, 185, 80), "merged": expand(163, 113, 247),
        "closed": expand(248, 81, 73)}
data = open(sys.argv[1], "rb").read()
offset = struct.unpack_from("<I", data, 10)[0]
step = struct.unpack_from("<H", data, 28)[0] // 8
pixels = data[offset:]
seen = collections.Counter()
for i in range(0, len(pixels) - step + 1, step):
    seen[(pixels[i + 2], pixels[i + 1], pixels[i])] += 1
missing = [name for name, colour in WANT.items() if seen.get(colour, 0) < 20]
if missing:
    sys.exit("pull-request state colours missing: %s" % ", ".join(missing))
print("GitHub state palette: " + ", ".join(
    "%s %dpx" % (name, seen[colour]) for name, colour in sorted(WANT.items())))
PYSTATES

# Both GitHub's GraphQL endpoint and the CDN in front of adsb.fi answer an
# HTTP/1.0 request by closing the connection, sending neither Content-Length
# nor Transfer-Encoding. Refusing such a body made healthy endpoints look dead,
# so a response with no framing headers at all must still parse.
UNFRAMED_FIXTURES="$TEST_DIR/unframed-fixtures"
mkdir -p "$UNFRAMED_FIXTURES"
for name in github-lists github-calendar; do
  python3 - "$ROOT/emulator/tests/fixtures/$name.http" \
           "$UNFRAMED_FIXTURES/$name.http" <<'PYSTRIP'
import sys
head, body = open(sys.argv[1], "rb").read().split(b"\r\n\r\n", 1)
kept = [l for l in head.split(b"\r\n")
        if not l.lower().startswith(b"content-length")]
open(sys.argv[2], "wb").write(b"\r\n".join(kept) + b"\r\n\r\n" + body)
PYSTRIP
done
if grep -qi 'content-length' "$UNFRAMED_FIXTURES/github-lists.http"; then
  echo "the unframed fixture still carries a Content-Length" >&2
  exit 1
fi

UNFRAMED_STATE="$TEST_DIR/unframed-state"
mkdir -p "$UNFRAMED_STATE/littlefs"
cp "$ROOT/emulator/tests/fixtures/github-config.json" \
  "$UNFRAMED_STATE/littlefs/config.json"
(cd "$ROOT" && "$ROOT/emulator/build/deskmate-emulator" \
  --headless --duration-ms 10000 --board esp8266 \
  --state-dir "$UNFRAMED_STATE" --web-port 0 \
  --responses "$UNFRAMED_FIXTURES" \
  --output "$TEST_DIR/github-unframed.bmp" --scale 1)
python3 - "$TEST_DIR/github-unframed.bmp" <<'PYUNFRAMED'
import struct, sys, collections
LEVELS = {(8, 69, 41), (0, 109, 49), (33, 166, 66), (57, 211, 82)}
data = open(sys.argv[1], "rb").read()
offset = struct.unpack_from("<I", data, 10)[0]
step = struct.unpack_from("<H", data, 28)[0] // 8
pixels = data[offset:]
seen = collections.Counter()
for i in range(0, len(pixels) - step + 1, step):
    seen[(pixels[i + 2], pixels[i + 1], pixels[i])] += 1
cells = sum(seen[c] for c in LEVELS)
if cells < 3000:
    sys.exit("a close-delimited response did not parse: %d heatmap cells" % cells)
print("Close-delimited responses parse: %d contribution cells from a body "
      "with no Content-Length" % cells)
PYUNFRAMED

# A calendar failure must be reported on the pulse page and must not touch the
# shared error state the other two pages draw from. Regression guard: the
# failure used to be written to the shared state and then erased by the next
# successful list fetch, leaving the page stuck on "LOADING" for good.
BAD_CAL_FIXTURES="$TEST_DIR/bad-calendar-fixtures"
mkdir -p "$BAD_CAL_FIXTURES"
cp "$ROOT/emulator/tests/fixtures/github-lists.http" "$BAD_CAL_FIXTURES/"
python3 - "$BAD_CAL_FIXTURES/github-calendar.http" <<'PYBADCAL'
import json, sys
body = json.dumps({"data": {"viewer": None}, "errors": [
    {"message": "Resource not accessible by personal access token",
     "type": "FORBIDDEN"}]}, separators=(",", ":"))
head = ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n" % len(body))
open(sys.argv[1], "wb").write((head + body).encode())
PYBADCAL

BAD_CAL_STATE="$TEST_DIR/bad-calendar"
mkdir -p "$BAD_CAL_STATE/littlefs"
sed 's/"pageInbox":true,"pagePulls":true/"pageInbox":false,"pagePulls":false/' \
  "$ROOT/emulator/tests/fixtures/github-config.json" \
  > "$BAD_CAL_STATE/littlefs/config.json"
(cd "$ROOT" && "$ROOT/emulator/build/deskmate-emulator" \
  --headless --duration-ms 6000 --board esp8266 \
  --state-dir "$BAD_CAL_STATE" --web-port 0 \
  --responses "$BAD_CAL_FIXTURES" \
  --output "$TEST_DIR/github-bad-calendar.bmp" --scale 1)
python3 - "$TEST_DIR/github-bad-calendar.bmp" <<'PYBADCHECK'
import struct, sys, collections

def expand(r, g, b):
    r5, g6, b5 = (r & 0xF8) >> 3, (g & 0xFC) >> 2, (b & 0xF8) >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))

ERROR = expand(248, 81, 73)
LINE = expand(43, 49, 58)
LEVELS = {expand(8, 69, 41), expand(0, 109, 49), expand(33, 166, 66),
          expand(57, 211, 82)}
data = open(sys.argv[1], "rb").read()
offset = struct.unpack_from("<I", data, 10)[0]
step = struct.unpack_from("<H", data, 28)[0] // 8
width = struct.unpack_from("<i", data, 18)[0]
height = abs(struct.unpack_from("<i", data, 22)[0])
pixels = data[offset:]

def at(x, y):  # BMP rows are bottom-up
    i = ((height - 1 - y) * width + x) * step
    return (pixels[i + 2], pixels[i + 1], pixels[i])

seen = collections.Counter()
for i in range(0, len(pixels) - step + 1, step):
    seen[(pixels[i + 2], pixels[i + 1], pixels[i])] += 1

if sum(seen[c] for c in LEVELS) > 100:
    sys.exit("a failed calendar still drew a heatmap")
if seen.get(ERROR, 0) < 10:
    sys.exit("the pulse page did not report the calendar failure")
# The header rule belongs to the action lists, which succeeded, so it must not
# have been recoloured by the calendar's failure.
rule = [at(x, 27) for x in range(20, 220, 20)]
if any(colour == ERROR for colour in rule):
    sys.exit("a calendar failure reddened the shared header rule")
if not any(colour == LINE for colour in rule):
    sys.exit("the header rule is missing entirely")
print("GitHub calendar failure isolated: pulse page reports it, "
      "header rule stays neutral")
PYBADCHECK

# Deselecting pages must remove them from the rotation. With only the pulse
# page selected, the capture that landed on the inbox above shows the heatmap
# instead, which also proves the dwell is derived rather than fixed.
PULSE_ONLY_STATE="$TEST_DIR/github-pulse-only"
mkdir -p "$PULSE_ONLY_STATE/littlefs"
sed 's/"pageInbox":true,"pagePulls":true/"pageInbox":false,"pagePulls":false/' \
  "$ROOT/emulator/tests/fixtures/github-config.json" \
  > "$PULSE_ONLY_STATE/littlefs/config.json"
(cd "$ROOT" && "$ROOT/emulator/build/deskmate-emulator" \
  --headless --duration-ms 2500 --board esp8266 \
  --state-dir "$PULSE_ONLY_STATE" --web-port 0 \
  --responses "$ROOT/emulator/tests/fixtures" \
  --output "$TEST_DIR/github-pulse-only.bmp" --scale 1)
python3 - "$TEST_DIR/github-pulse-only.bmp" "$TEST_DIR/github-inbox.bmp" <<'PYPAGES'
import struct, sys
LEVELS = {(8, 69, 41), (0, 109, 49), (33, 166, 66), (57, 211, 82)}

def levels(path):
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    step = struct.unpack_from("<H", data, 28)[0] // 8
    pixels = data[offset:]
    return sum(1 for i in range(0, len(pixels) - step + 1, step)
               if (pixels[i + 2], pixels[i + 1], pixels[i]) in LEVELS)

only, inbox = levels(sys.argv[1]), levels(sys.argv[2])
if only < 3000:
    sys.exit("pulse-only rotation did not show the pulse page (%d cells)" % only)
if inbox >= 3000:
    sys.exit("the inbox capture unexpectedly showed the heatmap (%d cells)" % inbox)
print("GitHub page selection honoured: %d cells with pulse only, %d on the inbox"
      % (only, inbox))
PYPAGES

# Without a token the same run must fall back to the credentials screen, which
# proves the fixture data is what reached the display above.
NO_TOKEN_STATE="$TEST_DIR/github-no-token"
mkdir -p "$NO_TOKEN_STATE/littlefs"
sed 's/"token":"fixture-token"/"token":""/' \
  "$ROOT/emulator/tests/fixtures/github-config.json" \
  > "$NO_TOKEN_STATE/littlefs/config.json"
(cd "$ROOT" && "$ROOT/emulator/build/deskmate-emulator" \
  --headless --duration-ms 4000 --board esp8266 \
  --state-dir "$NO_TOKEN_STATE" --web-port 0 \
  --responses "$ROOT/emulator/tests/fixtures" \
  --output "$TEST_DIR/github-no-token.bmp" --scale 1)
if cmp --silent "$TEST_DIR/github-inbox.bmp" "$TEST_DIR/github-no-token.bmp"; then
  echo "GitHub inbox rendered identically with and without a token" >&2
  exit 1
fi

echo
echo "Emulator tests passed: connectivity recovery, board boot, portal migration, airport settings, filesystem listing, JSON scanning, and the GitHub screen."
