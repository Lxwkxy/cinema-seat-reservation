#!/usr/bin/env bash

# Shared process and queue lifecycle helpers for project experiments.

experiment_process_matches() {
  local pid="$1"
  local expected_executable="$2"
  if [ -z "$expected_executable" ] || [ ! -r "/proc/$pid/exe" ]; then
    return 1
  fi
  [ "$(readlink "/proc/$pid/exe" 2>/dev/null || true)" = "$expected_executable" ]
}

experiment_wait_for_queue_gone() {
  local queue_path="$1"
  local timeout_seconds="$2"
  local deadline=$((SECONDS + timeout_seconds))
  while (( SECONDS < deadline )); do
    if [ ! -e "$queue_path" ]; then
      return 0
    fi
    sleep 0.1
  done
  [ ! -e "$queue_path" ]
}

experiment_wait_for_server() {
  local pid="$1"
  local server_log="$2"
  local expected_executable="$3"
  local timeout_seconds="$4"
  local deadline=$((SECONDS + timeout_seconds))
  while (( SECONDS < deadline )); do
    if ! kill -0 "$pid" 2>/dev/null; then
      return 1
    fi
    # The server writes the startup banner in more than one stream operation.
    # Wait for both lines so a partial "Server started:" write cannot trigger
    # a readiness decision while the process is still completing startup.
    if grep -q "Server started:" "$server_log" 2>/dev/null &&
       grep -q "Request queue:" "$server_log" 2>/dev/null &&
       experiment_process_matches "$pid" "$expected_executable"; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

experiment_write_server_diagnostic() {
  local pid="$1"
  local server_log="$2"
  local expected_executable="$3"
  local queue_path="$4"
  local reason="$5"
  {
    echo "experiment_diagnostic=$reason"
    echo "server_pid=$pid"
    echo "expected_executable=$expected_executable"
    if [ -r "/proc/$pid/exe" ]; then
      echo "actual_executable=$(readlink "/proc/$pid/exe" 2>/dev/null || true)"
    else
      echo "actual_executable=not_running"
    fi
    if [ -r "/proc/$pid/cmdline" ]; then
      echo -n "cmdline="
      tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true
      echo
    else
      echo "cmdline=not_running"
    fi
    if [ -e "$queue_path" ]; then
      echo "request_queue=present"
    else
      echo "request_queue=absent"
    fi
  } >> "$server_log"
}

experiment_stop_owned_server() {
  local pid="$1"
  local expected_executable="$2"
  local queue_path="$3"
  local timeout_seconds="$4"
  if ! kill -0 "$pid" 2>/dev/null; then
    wait "$pid" 2>/dev/null || true
    experiment_wait_for_queue_gone "$queue_path" "$timeout_seconds"
    return $?
  fi
  if ! experiment_process_matches "$pid" "$expected_executable"; then
    echo "Refusing to signal PID $pid because it is not the experiment server" >&2
    return 1
  fi

  kill -TERM "$pid" 2>/dev/null || true
  local deadline=$((SECONDS + timeout_seconds))
  while (( SECONDS < deadline )); do
    if ! kill -0 "$pid" 2>/dev/null ||
       ! experiment_process_matches "$pid" "$expected_executable"; then
      wait "$pid" 2>/dev/null || true
      experiment_wait_for_queue_gone "$queue_path" "$timeout_seconds"
      return $?
    fi
    sleep 0.2
  done

  if experiment_process_matches "$pid" "$expected_executable"; then
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  return 1
}
