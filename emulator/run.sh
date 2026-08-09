#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROGRAM="$ROOT/emulator/build/deskmate-emulator"

if [[ "${1:-}" == "--watch" ]]; then
  shift
  exec "$ROOT/emulator/watch.sh" "$@"
fi

if [[ ! -x "$PROGRAM" ]]; then
  "$ROOT/emulator/build.sh"
fi

cd "$ROOT"
while true; do
  set +e
  "$PROGRAM" "$@"
  status=$?
  set -e
  [[ $status -eq 75 ]] || exit "$status"
done
