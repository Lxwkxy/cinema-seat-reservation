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
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
RESULTS_ROOT="${RESULTS_ROOT:-$PROJECT_DIR/results}"
mkdir -p "$RESULTS_ROOT" || exit 1
RUN_DIR="$(mktemp -d "$RESULTS_ROOT/load-test-XXXXXXXX")" || exit 1
echo "Load test reports: $RUN_DIR"
ENVIRONMENT=native-linux
if [ -e /.dockerenv ]; then
  ENVIRONMENT=docker
elif grep -qi microsoft /proc/version 2>/dev/null; then
  ENVIRONMENT=wsl
fi

for ((clients=STEP; MAX_CLIENTS == 0 || clients <= MAX_CLIENTS; clients+=STEP)); do
  echo "=== Load Test: $clients logical clients ==="

  TRIAL_DIR="$RUN_DIR/clients-$clients"
  mkdir -p "$TRIAL_DIR" || exit 1
  "$PROJECT_DIR/bin/client_load" \
    --clients "$clients" \
    --requests "$REQUESTS_PER_CLIENT" \
    --command STATUS \
    --resource 10 \
    --report "$TRIAL_DIR/report.txt" \
    --experiment load_test > "$TRIAL_DIR/client.txt" 2>&1

  status=$?
  cat "$TRIAL_DIR/client.txt"
  if [ ! -s "$TRIAL_DIR/report.txt" ]; then
    printf 'Load test result report\nClients: %s\nNo per-client results were recorded. See client.txt.\n' "$clients" > "$TRIAL_DIR/report.txt"
  fi
  printf '\nEnvironment: %s\nClient exit code: %s\n' "$ENVIRONMENT" "$status" >> "$TRIAL_DIR/report.txt"
  echo "Report: $TRIAL_DIR/report.txt"

  echo "load_test_clients=$clients exit_status=$status"

  if [ "$status" -ne 0 ]; then
    echo "Load test stopped at $clients clients (exit status $status; see metrics above)"
    last_status="$status"
    break
  fi
done

exit "$last_status"
