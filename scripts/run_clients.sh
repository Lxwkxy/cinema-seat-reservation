#!/usr/bin/env bash
set -euo pipefail

CLIENT_COUNT=5
if [ "$#" -ge 1 ]; then CLIENT_COUNT="$1"; fi
if [ "$#" -gt 1 ] || ! [[ "$CLIENT_COUNT" =~ ^[1-9][0-9]*$ ]]; then
  echo "Usage: $0 [POSITIVE_CLIENT_COUNT]" >&2
  exit 1
fi

for ((id=1; id<=CLIENT_COUNT; ++id)); do
  echo "Open another terminal and run: ./bin/client --id $id"
done
