#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROGRAM="$ROOT/preview/build/deskmate-preview"
SCREEN="${1:-weather-clear}"

if ! command -v inotifywait >/dev/null 2>&1; then
  echo "watch.sh needs inotify-tools: sudo dnf install inotify-tools"
  exit 1
fi

while true; do
  "$ROOT/preview/build.sh"
  "$PROGRAM" --screen "$SCREEN" &
  PID=$!

  inotifywait -qq -r -e close_write,create,delete,move \
    --exclude 'preview/build|\.git|\.pio' \
    "$ROOT/src" "$ROOT/preview/src" "$ROOT/preview/include"

  kill "$PID" 2>/dev/null || true
  wait "$PID" 2>/dev/null || true
done
