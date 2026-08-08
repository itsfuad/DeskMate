#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROGRAM="$ROOT/preview/build/deskmate-preview"
SCREEN="${1:-weather-cycle-clear}"

build_preview() {
  while ! "$ROOT/preview/build.sh"; do
    echo "Preview build failed; retrying after the next edit..."
    sleep 2
  done
}

start_preview() {
  "$PROGRAM" --screen "$SCREEN" &
  PREVIEW_PID=$!
}

stop_preview() {
  kill "$PREVIEW_PID" 2>/dev/null || true
  wait "$PREVIEW_PID" 2>/dev/null || true
}

snapshot_sources() {
  find "$ROOT/src" "$ROOT/preview" "$ROOT/platformio.ini" \
    -type f \
    ! -path "$ROOT/preview/build/*" \
    ! -path "$ROOT/preview-output/*" \
    ! -path "$ROOT/preview/weather-cycle-demo.gif" \
    -printf '%p %T@ %s\n' 2>/dev/null | sha256sum
}

build_preview
start_preview
trap stop_preview EXIT

if command -v inotifywait >/dev/null 2>&1; then
  while true; do
    inotifywait -qq -r -e close_write,create,delete,move \
      --exclude '(^|/)(build|preview-output|\.git|\.pio)(/|$)' \
      "$ROOT/src" "$ROOT/preview" "$ROOT/platformio.ini"
    stop_preview
    build_preview
    start_preview
  done
fi

echo "inotifywait not found; using portable polling watcher."
previous="$(snapshot_sources)"
while true; do
  sleep 1
  current="$(snapshot_sources)"
  if [[ "$current" == "$previous" ]]; then
    continue
  fi
  previous="$current"
  stop_preview
  build_preview
  start_preview
done
