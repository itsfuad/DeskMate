#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/emulator/build"

CCACHE_DISABLE=1 cmake -S "$ROOT/emulator" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
CCACHE_DISABLE=1 cmake --build "$BUILD_DIR" --parallel

echo
echo "Built: $BUILD_DIR/deskmate-emulator"
