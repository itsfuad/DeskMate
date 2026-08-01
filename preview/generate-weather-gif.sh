#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCREEN="${1:-weather-cycle-clear}"
OUTPUT="${2:-$ROOT/preview/weather-cycle-demo.gif}"

case "$SCREEN" in
  weather-cycle-clear|weather-cycle-partly|weather-cycle-cloudy|weather-cycle-rain) ;;
  *)
    echo "Unknown weather cycle: $SCREEN" >&2
    echo "Use weather-cycle-clear, weather-cycle-partly, weather-cycle-cloudy, or weather-cycle-rain." >&2
    exit 1
    ;;
esac

if ! command -v magick >/dev/null 2>&1; then
  echo "generate-weather-gif.sh needs ImageMagick's magick command." >&2
  exit 1
fi

FRAME_DIR="$(mktemp -d -t deskmate-weather-gif-XXXXXX)"
trap 'rm -r -- "$FRAME_DIR"' EXIT

CCACHE_DISABLE=1 "$ROOT/preview/build.sh"

# 72 frames spaced by 1,000 ms cover the simulated 24-hour cycle once.
for FRAME_INDEX in {0..71}; do
  printf -v FRAME_NAME '%03d' "$FRAME_INDEX"
  "$ROOT/preview/run.sh" \
    --headless \
    --screen "$SCREEN" \
    --frame-ms "$((FRAME_INDEX * 1000))" \
    --output "$FRAME_DIR/$FRAME_NAME.bmp" \
    --scale 2 >/dev/null
done

mkdir -p "$(dirname "$OUTPUT")"
magick -delay 13 -loop 0 "$FRAME_DIR"/*.bmp "$OUTPUT"

echo
echo "Generated: $OUTPUT"
