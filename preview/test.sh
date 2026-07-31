#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="$(mktemp -d -t deskmate-preview-XXXXXX)"
trap 'rm -rf "$OUTPUT_DIR"' EXIT

"$ROOT/preview/build.sh"
"$ROOT/preview/run.sh" --all "$OUTPUT_DIR" --scale 1

expected="$($ROOT/preview/run.sh --list | wc -l)"
actual="$(find "$OUTPUT_DIR" -maxdepth 1 -type f -name '*.bmp' | wc -l)"

if [[ "$actual" -ne "$expected" ]]; then
  echo "Expected $expected screenshots, generated $actual" >&2
  exit 1
fi

for image in "$OUTPUT_DIR"/*.bmp; do
  size="$(stat -c '%s' "$image")"
  if [[ "$size" -ne 230454 ]]; then
    echo "Unexpected 240x240 BMP size ($size): $image" >&2
    exit 1
  fi
done

echo
printf 'Preview test passed: %s fixtures rendered at 240x240 RGB565.\n' "$actual"
