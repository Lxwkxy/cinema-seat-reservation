#!/usr/bin/env bash
set -u -o pipefail

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
    --resource 10

  status=$?

  echo "load_test_clients=$clients exit_status=$status"

  if [ "$status" -ne 0 ]; then
    echo "Load test stopped at $clients clients (exit status $status; see metrics above)"
    last_status="$status"
    break
  fi
done

exit "$last_status"
