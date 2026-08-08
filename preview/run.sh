#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROGRAM="$ROOT/preview/build/deskmate-preview"

if [[ "${1:-}" == "--watch" ]]; then
  shift
  exec "$ROOT/preview/watch.sh" "${1:-weather-cycle-clear}"
fi

if [[ ! -x "$PROGRAM" ]]; then
  "$ROOT/preview/build.sh"
fi

exec "$PROGRAM" "$@"
