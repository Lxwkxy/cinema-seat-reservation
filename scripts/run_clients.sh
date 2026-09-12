#!/usr/bin/env bash
set -euo pipefail

CLIENT_COUNT=5
if [ "$#" -ge 1 ]; then CLIENT_COUNT="$1"; fi

for id in $(seq 1 "$CLIENT_COUNT"); do
  echo "Open another terminal and run: ./bin/client --id $id"
done

