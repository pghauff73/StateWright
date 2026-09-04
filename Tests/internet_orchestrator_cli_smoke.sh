#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: %s STATEWRIGHT\n' "$0" >&2
  exit 2
fi

statewright=$1
workspace=$(mktemp -d /tmp/statewright-orchestrator-cli.XXXXXX)
trap 'rm -rf "$workspace"' EXIT

run() {
  local output
  output=$($statewright internet-improvement "$1")
  jq -e '.ok == true' <<<"$output" >/dev/null
  printf '%s\n' "$output"
}

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
  user_agent: "StateWright-Orchestrator-Smoke/1"
}')

watch=$($statewright internet-watch "$(jq -cn \
  --arg workspace "$workspace" --argjson policy "$policy" '{
    workspace: $workspace,
    action: "register",
    source_policy: $policy,
    canonical_url: "https://example.com/orchestrator-smoke",
    source_group: "example.com"
  }')")
jq -e '.ok == true' <<<"$watch" >/dev/null

request=$(jq -cn --arg workspace "$workspace" '{
  workspace: $workspace,
  current_timestamp: "2026-09-04T00:00:00Z",
  cycle_key: "2026-09-04T00:00:00Z",
  worker_id: "cli-smoke-worker",
  action_lease_expires_at: "2026-09-04T00:01:00Z",
  fetch_lease_expires_at: "2026-09-04T00:01:00Z",
  policy: {action_deadline: "2026-09-04T00:05:00Z"}
}')

plan=$(run "$(jq -c '. + {action:"plan"}' <<<"$request")")
jq -e '.result.plan.actions[0].kind == "SCHEDULE_FETCH"' \
  <<<"$plan" >/dev/null

result=$(run "$(jq -c '. + {action:"run-once"}' <<<"$request")")
action_key=$(jq -er '.result.action_key' <<<"$result")
run_id=$(jq -er '.result.run_id' <<<"$result")
jq -e '.result.status == "COMPLETED" and
       (.result.action_receipt_id | startswith("internet-improvement-action-receipt:sha256:"))' \
  <<<"$result" >/dev/null

explanation=$(run "$(jq -cn --arg workspace "$workspace" \
  --arg action_key "$action_key" \
  '{workspace:$workspace,action:"explain-action",action_key:$action_key}')")
jq -e '.result.eligible == true' <<<"$explanation" >/dev/null

status=$(run "$(jq -cn --arg workspace "$workspace" --arg run_id "$run_id" \
  '{workspace:$workspace,action:"run-status",run_id:$run_id}')")
jq -e '(.result.plans | length) == 1 and
       (.result.runs | length) == 1 and
       (.result.action_receipts | length) == 1' <<<"$status" >/dev/null

if $statewright internet-improvement "$(jq -cn --arg workspace "$workspace" \
  '{workspace:$workspace,action:"approve"}')" >/dev/null 2>&1; then
  printf 'internet improvement unexpectedly accepted approve\n' >&2
  exit 1
fi
