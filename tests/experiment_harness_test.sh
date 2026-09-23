#!/usr/bin/env bash

set -u -o pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/../scripts/experiment_helpers.sh"

tmp_root="${TMPDIR:-/tmp}"
test_dir="$(mktemp -d "$tmp_root/os-reservation-harness.XXXXXX")"

cleanup() {
  if [ -n "${child_pid:-}" ]; then
    kill -KILL "$child_pid" 2>/dev/null || true
    wait "$child_pid" 2>/dev/null || true
  fi
  rm -rf "$test_dir"
}
trap cleanup EXIT

fail() {
  echo "FAIL: $1" >&2
  exit 1
}

queue_path="$test_dir/request_queue"
touch "$queue_path"
if experiment_wait_for_queue_gone "$queue_path" 1; then
  fail "wait_for_queue_gone returned success while queue was present"
fi
rm -f "$queue_path"

expected_shell="$(readlink -f "$(command -v bash)")"
expected_sleep="$(readlink -f "$(command -v sleep)")"
ready_log="$test_dir/server.log"
sleep 5 &
child_pid=$!
ready_writer_pid=0
(
  sleep 0.1
  printf 'Server started: test\n' > "$ready_log"
  sleep 0.1
  printf 'Request queue: /test_queue\n' >> "$ready_log"
) &
ready_writer_pid=$!
if ! experiment_process_matches "$child_pid" "$expected_sleep"; then
  fail "process identity check rejected the expected process"
fi
if ! experiment_wait_for_server "$child_pid" "$ready_log" "$expected_sleep" 1; then
  fail "ready server was not detected"
fi
wait "$ready_writer_pid" 2>/dev/null || true
kill -KILL "$child_pid" 2>/dev/null || true
wait "$child_pid" 2>/dev/null || true
unset child_pid
unset ready_writer_pid

queue_path="$test_dir/owned_queue"
touch "$queue_path"
# Keep signal delivery deterministic; the separate remover models server mq_unlink.
bash -c 'trap '\''exit 0'\'' TERM; while true; do :; done' bash &
child_pid=$!
(sleep 0.2; rm -f "$queue_path") &
queue_remover_pid=$!
if ! experiment_stop_owned_server "$child_pid" "$expected_shell" "$queue_path" 3; then
  fail "owned server was not stopped and its queue was not removed"
fi
wait "$queue_remover_pid" 2>/dev/null || true
unset child_pid

queue_path="$test_dir/persistent_queue"
touch "$queue_path"
bash -c 'trap '\''while true; do :; done'\'' TERM; while true; do :; done' \
  bash &
child_pid=$!
if experiment_stop_owned_server "$child_pid" "$expected_shell" "$queue_path" 1; then
  fail "cleanup succeeded while queue remained present"
fi

echo "experiment_harness_test: PASS"
