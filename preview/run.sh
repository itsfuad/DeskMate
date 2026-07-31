#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROGRAM="$ROOT/preview/build/deskmate-preview"

if [[ ! -x "$PROGRAM" ]]; then
  "$ROOT/preview/build.sh"
fi

exec "$PROGRAM" "$@"
