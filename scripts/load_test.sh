#!/usr/bin/env bash
set -u -o pipefail

mkdir -p results

# 0 means unlimited. Set MAX_CLIENTS to cap the experiment if needed.
MAX_CLIENTS="${MAX_CLIENTS:-0}"
REQUESTS_PER_CLIENT="${REQUESTS_PER_CLIENT:-20}"
STEP="${STEP:-5}"

if ! [[ "$MAX_CLIENTS" =~ ^[0-9]+$ && "$REQUESTS_PER_CLIENT" =~ ^[1-9][0-9]*$ && "$STEP" =~ ^[1-9][0-9]*$ ]]; then
  echo "MAX_CLIENTS must be >= 0; REQUESTS_PER_CLIENT and STEP must be positive integers" >&2
  exit 1
fi

last_status=0

for ((clients=STEP; MAX_CLIENTS == 0 || clients <= MAX_CLIENTS; clients+=STEP)); do
  echo "=== Load Test: $clients logical clients ==="

  ./bin/client_load \
    --clients "$clients" \
    --requests "$REQUESTS_PER_CLIENT" \
    --command STATUS \
    --resource 10 | tee "results/load_$clients.txt"

  status=${PIPESTATUS[0]}

  echo "load_test_clients=$clients exit_status=$status" \
    | tee -a "results/load_$clients.txt"

  if [ "$status" -ne 0 ]; then
    echo "System reached failure/timeout at $clients clients"
    last_status="$status"
    break
  fi
done

exit "$last_status"
