#!/usr/bin/env bash

set -u -o pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SERVER="${SERVER:-$PROJECT_DIR/bin/server}"
LOAD_CLIENT="${LOAD_CLIENT:-$PROJECT_DIR/bin/client_load}"
EVIDENCE_ROOT="${EVIDENCE_ROOT:-$PROJECT_DIR/docs/experiment-evidence}"

SEQUENTIAL_CLIENTS=5
RACE_CLIENTS="${RACE_CLIENTS:-20}"
RACE_RETRIES="${RACE_RETRIES:-5}"
WORKERS=3
REQUESTS_PER_CLIENT=1
READY_TIMEOUT_SECONDS="${READY_TIMEOUT_SECONDS:-10}"
STOP_TIMEOUT_SECONDS="${STOP_TIMEOUT_SECONDS:-15}"
QUEUE_GONE_TIMEOUT_SECONDS="${QUEUE_GONE_TIMEOUT_SECONDS:-15}"
REQUEST_QUEUE_PATH="/dev/mqueue/osproj_requests"
RUN_ID="${RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)_$$}"

if ! [[ "$RACE_CLIENTS" =~ ^[1-9][0-9]*$ &&
         "$RACE_RETRIES" =~ ^[1-9][0-9]*$ &&
         "$READY_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ &&
         "$STOP_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ &&
         "$QUEUE_GONE_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
  echo "Client count, retry count and timeouts must be positive integers" >&2
  exit 1
fi
if ! [[ "$RUN_ID" =~ ^[A-Za-z0-9_.-]+$ ]]; then
  echo "RUN_ID may contain only letters, numbers, underscore, dot and hyphen" >&2
  exit 1
fi
if [ ! -x "$SERVER" ] || [ ! -x "$LOAD_CLIENT" ]; then
  echo "Build bin/server and bin/client_load before running project experiments" >&2
  exit 1
fi
if [ -e /.dockerenv ]; then
  ENVIRONMENT="docker"
elif grep -qi microsoft /proc/version 2>/dev/null; then
  ENVIRONMENT="wsl"
else
  ENVIRONMENT="native-linux"
fi

. "$SCRIPT_DIR/experiment_helpers.sh"

RUN_DIR="$EVIDENCE_ROOT/$RUN_ID"
if [ -e "$RUN_DIR" ]; then
  echo "Refusing to overwrite existing experiment evidence: $RUN_DIR" >&2
  exit 1
fi
if [ -e "$REQUEST_QUEUE_PATH" ]; then
  echo "Refusing to run: $REQUEST_QUEUE_PATH already exists; it will not be touched" >&2
  exit 1
fi
mkdir -p "$RUN_DIR"

ALL_RESULTS="$RUN_DIR/results.csv"
SUMMARY="$RUN_DIR/summary.txt"
METADATA="$RUN_DIR/environment.txt"
SERVER_PID=0
SERVER_EXECUTABLE="$(readlink -f "$SERVER" 2>/dev/null || true)"
RUN_ABORTED=0
RACE_OBSERVED=0
EXP1_STATUS="not_run"
EXP3_STATUS="not_run"
LAST_STATUS="not_run"
LAST_OUTCOME="not_run"

cat > "$METADATA" <<EOF
run_id=$RUN_ID
environment=$ENVIRONMENT
started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
server_binary=$SERVER
sequential_server_command=$SERVER --workers 1 --sync on --delay off --verbose on
sequential_client_command=$LOAD_CLIENT --clients $SEQUENTIAL_CLIENTS --requests 1 --command RESERVE --resource 10
race_server_command=$SERVER --workers $WORKERS --sync off --delay on --verbose on
race_client_command=$LOAD_CLIENT --clients $RACE_CLIENTS --requests 1 --command RESERVE --resource 10
synchronized_server_command=$SERVER --workers $WORKERS --sync on --delay on --verbose on
synchronized_client_command=$LOAD_CLIENT --clients $RACE_CLIENTS --requests 1 --command RESERVE --resource 10
race_retry_limit=$RACE_RETRIES
cleanup_policy=Only the server process started by this script is stopped; an existing request queue causes an abort and is never unlinked
EOF

CSV_HEADER="experiment,attempt,environment,workers,sync,delay,clients,requests_per_client,planned_requests,attempted_requests,success,rejected,timeouts,transport_errors,setup_errors,client_exit,status,outcome,failure_reason,server_command,client_command"
printf '%s\n' "$CSV_HEADER" > "$ALL_RESULTS"

append_csv_row() {
  local output_file="$1"
  shift
  local first=1
  local field
  for field in "$@"; do
    if [ "$first" -eq 0 ]; then
      printf ',' >> "$output_file"
    fi
    printf '%s' "$field" >> "$output_file"
    first=0
  done
  printf '\n' >> "$output_file"
}

metric_value() {
  local metric_file="$1"
  local key="$2"
  awk -v wanted="$key" '{
    for (field_index = 1; field_index <= NF; ++field_index) {
      split($field_index, pair, "=")
      if (pair[1] == wanted) {
        print pair[2]
        exit
      }
    }
  }' "$metric_file"
}

cleanup_server() {
  if [ "$SERVER_PID" -eq 0 ]; then
    return 0
  fi
  if experiment_stop_owned_server "$SERVER_PID" "$SERVER_EXECUTABLE" \
       "$REQUEST_QUEUE_PATH" "$STOP_TIMEOUT_SECONDS"; then
    SERVER_PID=0
    return 0
  fi
  SERVER_PID=0
  return 1
}
trap cleanup_server EXIT INT TERM

run_trial() {
  local experiment="$1"
  local attempt="$2"
  local workers="$3"
  local sync_mode="$4"
  local delay_mode="$5"
  local clients="$6"
  local planned=$((clients * REQUESTS_PER_CLIENT))
  local tag="${experiment}_attempt_${attempt}"
  local report_dir="$RUN_DIR/$tag"
  mkdir -p "$report_dir" || return 1
  local report="$report_dir/report.txt"
  local server_log="$RUN_DIR/${tag}.server.log"
  local client_output="$RUN_DIR/${tag}.client.txt"
  local server_command="$SERVER --workers $workers --sync $sync_mode --delay $delay_mode --verbose on"
  local client_command="$LOAD_CLIENT --clients $clients --requests $REQUESTS_PER_CLIENT --command RESERVE --resource 10 --report $report --experiment $experiment"
  local attempted=0
  local success=0
  local rejected=0
  local timeouts=0
  local transport_errors=0
  local setup_errors=0
  local client_status=NA
  local trial_status="failed"
  local outcome="setup_error"
  local failure_reason=""

  : > "$server_log"
  : > "$client_output"

  if [ "$RUN_ABORTED" -ne 0 ]; then
    failure_reason="not_run_after_cleanup_failure"
  elif [ -e "$REQUEST_QUEUE_PATH" ]; then
    failure_reason="request_queue_exists_before_start"
    RUN_ABORTED=1
  else
    "$SERVER" --workers "$workers" --sync "$sync_mode" \
      --delay "$delay_mode" --verbose on > "$server_log" 2>&1 &
    SERVER_PID=$!
    if experiment_wait_for_server "$SERVER_PID" "$server_log" \
         "$SERVER_EXECUTABLE" "$READY_TIMEOUT_SECONDS"; then
      if "$LOAD_CLIENT" --clients "$clients" --requests "$REQUESTS_PER_CLIENT" \
           --command RESERVE --resource 10 --report "$report" \
           --experiment "$experiment" > "$client_output" 2>&1; then
        client_status=0
      else
        client_status=$?
      fi

      attempted="$(metric_value "$client_output" attempted_requests)"
      success="$(metric_value "$client_output" success)"
      rejected="$(metric_value "$client_output" rejected)"
      timeouts="$(metric_value "$client_output" timeouts)"
      transport_errors="$(metric_value "$client_output" transport_errors)"
      setup_errors="$(metric_value "$client_output" setup_errors)"

      if [ "$client_status" -ne 0 ] && [ "$client_status" -ne 3 ]; then
        failure_reason="client_exit_$client_status"
      elif [ "$attempted" -ne "$planned" ] ||
           [ "$timeouts" -ne 0 ] || [ "$transport_errors" -ne 0 ] ||
           [ "$setup_errors" -ne 0 ]; then
        failure_reason="client_requests_incomplete_or_transport_error"
      else
        trial_status="complete"
        outcome="no_race_observed"
        if [ "$experiment" = "exp1_sequential" ]; then
          if [ "$success" -eq 1 ] && [ "$rejected" -eq $((clients - 1)) ]; then
            outcome="single_reservation_success"
          else
            trial_status="failed"
            failure_reason="sequential_expected_one_success"
            outcome="unexpected_reservation_count"
          fi
        elif [ "$experiment" = "exp2_without_sync" ]; then
          if [ "$success" -gt 1 ]; then
            outcome="race_observed"
          fi
        elif [ "$experiment" = "exp3_with_sync" ]; then
          if [ "$success" -eq 1 ] && [ "$rejected" -eq $((clients - 1)) ]; then
            outcome="single_reservation_success"
          else
            trial_status="failed"
            failure_reason="synchronized_expected_one_success"
            outcome="unexpected_reservation_count"
          fi
        fi
      fi
    else
      failure_reason="server_not_ready"
      experiment_write_server_diagnostic "$SERVER_PID" "$server_log" \
        "$SERVER_EXECUTABLE" "$REQUEST_QUEUE_PATH" "$failure_reason"
    fi
  fi

  if [ "$SERVER_PID" -ne 0 ]; then
    if ! cleanup_server; then
      trial_status="failed"
      outcome="cleanup_error"
      if [ -n "$failure_reason" ]; then
        failure_reason="$failure_reason;server_cleanup_or_queue_failed"
      else
        failure_reason="server_cleanup_or_queue_failed"
      fi
      RUN_ABORTED=1
    fi
  fi

  if [ "$trial_status" = "complete" ] && [ "$experiment" = "exp2_without_sync" ] &&
     [ "$outcome" = "race_observed" ]; then
    RACE_OBSERVED=1
  fi

  if [ "$trial_status" = "failed" ] && [ -z "$failure_reason" ]; then
    failure_reason="unknown_failure"
  fi

  append_csv_row "$ALL_RESULTS" "$experiment" "$attempt" "$ENVIRONMENT" \
    "$workers" "$sync_mode" "$delay_mode" "$clients" \
    "$REQUESTS_PER_CLIENT" "$planned" "$attempted" "$success" "$rejected" \
    "$timeouts" "$transport_errors" "$setup_errors" "$client_status" \
    "$trial_status" "$outcome" "$failure_reason" "$server_command" "$client_command"

  if [ ! -s "$report" ]; then
    printf 'Concurrent reservation result report\nExperiment: %s\nTarget seat: 10\n\nNo per-client results were recorded.\n' "$experiment" > "$report"
  fi
  printf '\nTrial status: %s\nOutcome: %s\nFailure reason: %s\nEnvironment: %s\nWorkers: %s\nSync: %s\nDelay: %s\nAttempt: %s\n' \
    "$trial_status" "$outcome" "$failure_reason" "$ENVIRONMENT" "$workers" \
    "$sync_mode" "$delay_mode" "$attempt" >> "$report"
  echo "Report: $report"
  LAST_STATUS="$trial_status"
  LAST_OUTCOME="$outcome"
  printf '[%s] %s attempt=%s clients=%s status=%s outcome=%s' \
    "$(date -u +%H:%M:%S)" "$experiment" "$attempt" "$clients" \
    "$trial_status" "$outcome"
  if [ -n "$failure_reason" ]; then
    printf ' reason=%s' "$failure_reason"
  fi
  printf '\n'
}

echo "Project experiments: $RUN_ID"
echo "Environment: $ENVIRONMENT"
echo "Evidence: $RUN_DIR"

run_trial exp1_sequential 1 1 on off "$SEQUENTIAL_CLIENTS"
EXP1_STATUS="$LAST_STATUS"

for ((attempt = 1; attempt <= RACE_RETRIES; ++attempt)); do
  run_trial exp2_without_sync "$attempt" "$WORKERS" off on "$RACE_CLIENTS"
  if [ "$RACE_OBSERVED" -ne 0 ] || [ "$RUN_ABORTED" -ne 0 ]; then
    break
  fi
done

run_trial exp3_with_sync 1 "$WORKERS" on on "$RACE_CLIENTS"
EXP3_STATUS="$LAST_STATUS"

{
  echo "C223 OS Project required reservation experiments"
  echo "run_id=$RUN_ID"
  echo "environment=$ENVIRONMENT"
  echo "Experiment 1: workers=1 sync=on delay=off clients=$SEQUENTIAL_CLIENTS"
  echo "Experiment 1 status=$EXP1_STATUS outcome=$(awk -F, '$1 == "exp1_sequential" { print $18; exit }' "$ALL_RESULTS")"
  echo "Experiment 2: workers=$WORKERS sync=off delay=on clients=$RACE_CLIENTS"
  echo "Experiment 2 race_observed=$RACE_OBSERVED attempts=$(awk -F, '$1 == "exp2_without_sync" { count++ } END { print count + 0 }' "$ALL_RESULTS")"
  echo "Experiment 3: workers=$WORKERS sync=on delay=on clients=$RACE_CLIENTS"
  echo "Experiment 3 status=$EXP3_STATUS outcome=$(awk -F, '$1 == "exp3_with_sync" { print $18; exit }' "$ALL_RESULTS")"
  echo "Detailed metrics and failure reasons are in results.csv; preserve all attempt logs."
} > "$SUMMARY"

echo "Summary: $SUMMARY"
echo "Results: $ALL_RESULTS"
if [ "$RUN_ABORTED" -ne 0 ] || [ "$EXP1_STATUS" != "complete" ] ||
   [ "$RACE_OBSERVED" -eq 0 ] || [ "$EXP3_STATUS" != "complete" ]; then
  echo "One or more required outcomes were not observed; evidence was retained." >&2
  exit 2
fi
