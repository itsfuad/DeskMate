#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d -t deskmate-emulator-XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT

"$ROOT/emulator/build.sh"
"$ROOT/emulator/build/deskmate-radar-trail-test"
"$ROOT/emulator/build/deskmate-radar-client-test" \
  "$ROOT/emulator/tests/fixtures" "$TEST_DIR/radar-state"

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

echo
echo "Emulator tests passed: board boot, portal migration, airport settings, and filesystem listing."
