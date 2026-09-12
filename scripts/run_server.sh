#!/usr/bin/env bash
set -euo pipefail

SYNC_MODE=on
WORKERS=3
DELAY_MODE=on

if [ "$#" -ge 1 ]; then SYNC_MODE="$1"; fi
if [ "$#" -ge 2 ]; then WORKERS="$2"; fi
if [ "$#" -ge 3 ]; then DELAY_MODE="$3"; fi

exec ./bin/server \
  --workers "$WORKERS" \
  --sync "$SYNC_MODE" \
  --delay "$DELAY_MODE"

