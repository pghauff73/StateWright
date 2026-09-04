#!/usr/bin/env bash
set -euo pipefail

supervisor=${1:?statewright-internet-supervisor executable is required}
statewright=${2:?statewright executable is required}

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

"$supervisor" --version \
  | grep -q '^statewright-internet-improvement-supervisor-v1$'

oversized_request="$root/oversized-request.json"
truncate -s 65537 "$oversized_request"
if "$supervisor" \
  --statewright "$statewright" \
  --workspace "$root/oversized-workspace" \
  --worker-id oversized-smoke \
  --request-file "$oversized_request" \
  >"$root/oversized.stdout" 2>"$root/oversized.stderr"; then
  printf 'supervisor unexpectedly accepted an oversized request file\n' >&2
  exit 1
fi
grep -q 'exceeds the 64 KiB configuration limit' "$root/oversized.stderr"

workspace="$root/workspace"
event_log="$root/events.jsonl"
stdout_log="$root/stdout.jsonl"
mkdir -p "$workspace"

"$supervisor" \
  --statewright "$statewright" \
  --workspace "$workspace" \
  --worker-id supervisor-smoke \
  --once \
  --child-timeout-seconds 5 \
  --action-lease-seconds 10 \
  --fetch-lease-seconds 10 \
  --action-deadline-seconds 5 \
  --success-delay-milliseconds 0 \
  --failure-backoff-initial-milliseconds 0 \
  --failure-backoff-maximum-milliseconds 0 \
  --event-log "$event_log" >"$stdout_log"

cmp "$stdout_log" "$event_log"
jq -s -e '
  map(.event_type) ==
    ["SUPERVISOR_STARTED", "CYCLE_STARTED", "RUN_RESULT",
     "SUPERVISOR_STOPPED"] and
  last.details.summary.successful == true and
  last.details.summary.final_status == "NO_ELIGIBLE_WORK" and
  last.details.summary.cycles_started == 1 and
  (last.details.summary.last_run_id |
    startswith("internet-improvement-run:sha256:"))
' "$event_log" >/dev/null

fake="$root/fake-statewright"
fake_pid_file="$root/fake.pid"
cat >"$fake" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$$" >"${STATEWRIGHT_SUPERVISOR_FAKE_PID_FILE:?}"
sleep 3
printf '%s\n' '{"ok":true,"result":{"action_leases":[],"action_receipts":[],"plans":[],"run_events":[],"runs":[]}}'
EOF
chmod +x "$fake"

timeout_log="$root/timeout.jsonl"
if STATEWRIGHT_SUPERVISOR_FAKE_PID_FILE="$fake_pid_file" "$supervisor" \
  --statewright "$fake" \
  --workspace "$root/timeout-workspace" \
  --worker-id timeout-smoke \
  --once \
  --maximum-failures 1 \
  --child-timeout-seconds 1 \
  --action-lease-seconds 2 \
  --fetch-lease-seconds 2 \
  --action-deadline-seconds 1 \
  --success-delay-milliseconds 0 \
  --failure-backoff-initial-milliseconds 0 \
  --failure-backoff-maximum-milliseconds 0 >"$timeout_log"; then
  printf 'supervisor unexpectedly accepted a timed-out child\n' >&2
  exit 1
fi

test -s "$fake_pid_file"
fake_pid=$(<"$fake_pid_file")
if kill -0 -- "-$fake_pid" 2>/dev/null; then
  printf 'timed-out child process group remains alive: %s\n' "$fake_pid" >&2
  exit 1
fi

jq -s -e '
  ([.[].event_type] | index("INVOCATION_FAILED")) != null and
  last.event_type == "SUPERVISOR_STOPPED" and
  last.details.summary.successful == false and
  last.details.summary.final_status == "CIRCUIT_OPEN" and
  last.details.summary.invocation_failures == 1 and
  (last.details.summary.diagnostic | contains("timed out"))
' "$timeout_log" >/dev/null

signal_pid_file="$root/signal-fake.pid"
signal_log="$root/signal.jsonl"
STATEWRIGHT_SUPERVISOR_FAKE_PID_FILE="$signal_pid_file" "$supervisor" \
  --statewright "$fake" \
  --workspace "$root/signal-workspace" \
  --worker-id signal-smoke \
  --once \
  --child-timeout-seconds 5 \
  --action-lease-seconds 6 \
  --fetch-lease-seconds 6 \
  --action-deadline-seconds 5 \
  --success-delay-milliseconds 0 \
  --failure-backoff-initial-milliseconds 0 \
  --failure-backoff-maximum-milliseconds 0 >"$signal_log" &
supervisor_pid=$!
for _ in $(seq 1 500); do
  if test -s "$signal_pid_file"; then
    break
  fi
  sleep 0.01
done
test -s "$signal_pid_file"
kill -TERM "$supervisor_pid"
wait "$supervisor_pid"
signal_child_pid=$(<"$signal_pid_file")
if kill -0 -- "-$signal_child_pid" 2>/dev/null; then
  printf 'cancelled child process group remains alive: %s\n' \
    "$signal_child_pid" >&2
  exit 1
fi
jq -s -e '
  ([.[].event_type] | index("INVOCATION_FAILED")) != null and
  last.event_type == "SUPERVISOR_STOPPED" and
  last.details.summary.successful == true and
  last.details.summary.final_status == "STOP_REQUESTED" and
  (last.details.summary.diagnostic | contains("cancelled"))
' "$signal_log" >/dev/null

printf 'internet supervisor smoke passed\n'
