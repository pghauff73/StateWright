#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: %s STATEWRIGHT\n' "$0" >&2
  exit 2
fi

statewright=$1
workspace=$(mktemp -d /tmp/statewright-orchestrator-fault.XXXXXX)
trap 'rm -rf "$workspace"' EXIT

policy=$(jq -cn '{
  schema_version: 1,
  policy_version: "statewright-internet-source-policy-v1",
  allowed_schemes: ["https"],
  allowed_ports: [443],
  accepted_mime_types: ["text/plain"],
  maximum_redirects: 1,
  maximum_header_bytes: 4096,
  maximum_response_bytes: 4096,
  maximum_decompressed_bytes: 4096,
  connect_timeout_seconds: 1,
  request_timeout_seconds: 1,
  require_tls_verification: true,
  allow_loopback_for_tests: false,
  require_robots_permission: true,
  require_known_license: true,
  user_agent: "StateWright-Orchestrator-Fault/1"
}')

watch=$($statewright internet-watch "$(jq -cn \
  --arg workspace "$workspace" --argjson policy "$policy" '{
    workspace: $workspace,
    action: "register",
    source_policy: $policy,
    canonical_url: "https://127.0.0.1/fail-closed",
    source_group: "fault.local"
  }')")
jq -e '.ok == true' <<<"$watch" >/dev/null

first=$($statewright internet-improvement "$(jq -cn --arg workspace "$workspace" '{
  workspace: $workspace,
  action: "run-once",
  current_timestamp: "2026-09-04T00:00:00Z",
  cycle_key: "2026-09-04T00:00:00Z",
  worker_id: "fault-worker",
  action_lease_expires_at: "2026-09-04T00:01:00Z",
  fetch_lease_expires_at: "2026-09-04T00:01:00Z",
  policy: {action_deadline: "2026-09-04T00:05:00Z"}
}')")
jq -e '.ok == true and .result.status == "COMPLETED"' <<<"$first" >/dev/null

second=$($statewright internet-improvement "$(jq -cn --arg workspace "$workspace" '{
  workspace: $workspace,
  action: "run-once",
  current_timestamp: "2026-09-04T00:00:02Z",
  cycle_key: "2026-09-04T00:00:00Z",
  worker_id: "fault-worker",
  action_lease_expires_at: "2026-09-04T00:01:02Z",
  fetch_lease_expires_at: "2026-09-04T00:01:02Z",
  policy: {action_deadline: "2026-09-04T00:05:00Z"}
}')")
jq -e '.ok == true and .result.status == "FAILED" and
       (.result.action_receipt_id | startswith("internet-improvement-action-receipt:sha256:"))' \
  <<<"$second" >/dev/null

status=$($statewright internet-improvement "$(jq -cn --arg workspace "$workspace" \
  '{workspace:$workspace,action:"run-status"}')")
jq -e '.ok == true and
       ([.result.action_receipts[].payload.terminal_state] | index("FAILED")) != null and
       ([.result.run_events[].payload.event_type] | index("ACTION_FAILED")) != null' \
  <<<"$status" >/dev/null
