#!/usr/bin/env bash
set -euo pipefail

SYNC_MODE=on
WORKERS=3
DELAY_MODE=on
VERBOSE_MODE=on

if [ "$#" -ge 1 ]; then SYNC_MODE="$1"; fi
if [ "$#" -ge 2 ]; then WORKERS="$2"; fi
if [ "$#" -ge 3 ]; then DELAY_MODE="$3"; fi
if [ "$#" -ge 4 ]; then VERBOSE_MODE="$4"; fi
if [ "$#" -gt 4 ]; then
  echo "Usage: $0 [SYNC] [WORKERS] [DELAY] [VERBOSE]" >&2
  exit 1
fi

exec ./bin/server \
  --workers "$WORKERS" \
  --sync "$SYNC_MODE" \
  --delay "$DELAY_MODE" \
  --verbose "$VERBOSE_MODE"
