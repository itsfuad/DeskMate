#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROGRAM="$ROOT/emulator/build/deskmate-emulator"
EMULATOR_PID=""

build_emulator() {
  while ! "$ROOT/emulator/build.sh"; do
    echo "Emulator build failed; waiting for the next source change."
    sleep 2
  done
}

start_emulator() {
  (cd "$ROOT" && "$PROGRAM" "$@") &
  EMULATOR_PID=$!
}

stop_emulator() {
  [[ -n "$EMULATOR_PID" ]] || return
  kill "$EMULATOR_PID" 2>/dev/null || true
  wait "$EMULATOR_PID" 2>/dev/null || true
  EMULATOR_PID=""
}

snapshot_sources() {
  find "$ROOT/src" "$ROOT/emulator" "$ROOT/platformio.ini" \
    -type f ! -path "$ROOT/emulator/build/*" ! -path "$ROOT/emulator/.state/*" \
    -printf '%p %T@ %s\n' 2>/dev/null | sha256sum
}

build_emulator
start_emulator "$@"
trap stop_emulator EXIT INT TERM

if command -v inotifywait >/dev/null 2>&1; then
  while true; do
    inotifywait -qq -r -e close_write,create,delete,move \
      --exclude '(^|/)(build|\.state|\.git|\.pio)(/|$)' \
      "$ROOT/src" "$ROOT/emulator" "$ROOT/platformio.ini"
    stop_emulator
    build_emulator
    start_emulator "$@"
  done
fi

echo "inotifywait not found; using the polling watcher."
previous="$(snapshot_sources)"
while true; do
  sleep 1
  current="$(snapshot_sources)"
  [[ "$current" == "$previous" ]] && continue
  previous="$current"
  stop_emulator
  build_emulator
  start_emulator "$@"
done
