#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CLIENT="$PROJECT_DIR/bin/client"

if [ "$#" -ne 0 ]; then
  echo "Usage: bash scripts/demo_clients.sh" >&2
  exit 1
fi
if [ ! -x "$CLIENT" ]; then
  echo "Run make first, then start a fresh server before this demo." >&2
  exit 1
fi

LOG_ROOT="${DEMO_LOG_ROOT:-$PROJECT_DIR/logs}"
mkdir -p "$LOG_ROOT"
LOG_DIR="$(mktemp -d "$LOG_ROOT/demo-clients-XXXXXXXX")"
echo "Logs: $LOG_DIR"

# Prepare a valid cancellation independently of concurrent request ordering.
if "$CLIENT" --id 4 --once RESERVE 2 >"$LOG_DIR/setup.log" 2>&1; then
  echo "Setup: Client-4 reserved Resource-2"
else
  cat "$LOG_DIR/setup.log"
  echo "Setup failed. Start a fresh server and retry." >&2
  exit 1
fi

commands=("LIST" "STATUS 1" "RESERVE 1" "CANCEL 2" "QUIT")
pids=()
# On interruption, reap this script's children; client timeouts bound their wait.
trap 'wait' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

for index in "${!commands[@]}"; do
  client_id=$((index + 1))
  read -r -a arguments <<< "${commands[index]}"
  "$CLIENT" --id "$client_id" --once "${arguments[@]}" \
    >"$LOG_DIR/client-$client_id.log" 2>&1 &
  pids+=("$!")
  echo "Client-$client_id PID=${pids[index]}: ${commands[index]}"
done

# All five are launched before waiting; separate files prevent mixed output.
failed=0
for index in "${!pids[@]}"; do
  client_id=$((index + 1))
  status=0
  if wait "${pids[index]}"; then
    status=0
  else
    status=$?
    failed=1
  fi
  echo "--- Client-$client_id: ${commands[index]} (exit=$status) ---"
  cat "$LOG_DIR/client-$client_id.log"
done

if [ "$failed" -ne 0 ]; then
  echo "Demo failed. Inspect the logs above." >&2
  exit 1
fi
echo "Demo passed: five client processes sent five different commands successfully."
