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

echo
echo "Emulator tests passed: real app booted for all board profiles."
