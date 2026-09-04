#!/usr/bin/env bash
set -euo pipefail

statewright=$1
workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT

manifest="$workspace/watchlist.json"
create_request=$(jq -nc --arg output "$manifest" '{
  action: "create",
  template: "w3c-recommendation",
  slugs: ["rdf-canon"],
  enabled: true,
  watchlist_version: "watchlist-cli-smoke-v1",
  output_path: $output
}')
create_response=$($statewright internet-watchlist "$create_request")
jq -e '.ok == true and .result.watch_count == 1' <<<"$create_response" >/dev/null

validate_request=$(jq -nc --arg manifest "$manifest" '{
  action: "validate",
  manifest_path: $manifest
}')
validate_response=$($statewright internet-watchlist "$validate_request")
jq -e '.ok == true and .result.valid == true' <<<"$validate_response" >/dev/null

manifest_sha=$($statewright hash-json "$(cat "$manifest")")
entry=$(jq -c '.watches[0]' "$manifest")
entry_sha=$($statewright hash-json "$entry")
report_material=$(jq -nc \
  --arg manifest_sha "$manifest_sha" \
  --arg entry_sha "$entry_sha" '{
    checked_at: "2026-09-04T12:00:00Z",
    manifest_sha256: $manifest_sha,
    results: [{
      blocking_reasons: [],
      canonical_url: "https://www.w3.org/TR/rdf-canon/",
      eligible: true,
      entry_name: "w3c-rdf-canon",
      entry_sha256: $entry_sha,
      status: "PREFLIGHT_ELIGIBLE",
      content_type: "text/html",
      final_url: "https://www.w3.org/TR/rdf-canon/",
      http_status: 200,
      provider_identity: "watchlist-cli-smoke-v1",
      resolved_addresses: ["93.184.216.34"],
      robots_allowed: true,
      robots_policy_evaluated: true,
      tls_verified: true
    }],
    schema_version: 1,
    watchlist_version: "watchlist-cli-smoke-v1"
  }')
report_signature=$($statewright hash-json "$report_material")
report="$workspace/preflight.json"
jq --arg signature "$report_signature" \
  '. + {report_signature: $signature}' <<<"$report_material" >"$report"

register_request=$(jq -nc \
  --arg workspace "$workspace/store" \
  --arg manifest "$manifest" \
  --arg report "$report" '{
    action: "register",
    workspace: $workspace,
    manifest_path: $manifest,
    preflight_report_path: $report,
    current_timestamp: "2026-09-04T12:00:00Z",
    enable_eligible: true
  }')
dry_run_request=$(jq -c '. + {dry_run: true}' <<<"$register_request")
dry_run=$($statewright internet-watchlist "$dry_run_request")
jq -e '.ok == true and .result.dry_run == true and (.result.registrations | length) == 1' <<<"$dry_run" >/dev/null
test ! -e "$workspace/store"

first=$($statewright internet-watchlist "$register_request")
second=$($statewright internet-watchlist "$register_request")
jq -e '.ok == true and .result.registrations[0].eligibility_status == "REGISTERED_ENABLED"' <<<"$first" >/dev/null
first_watch=$(jq -r '.result.registrations[0].watch_id' <<<"$first")
second_watch=$(jq -r '.result.registrations[0].watch_id' <<<"$second")
first_registration=$(jq -r '.result.registrations[0].registration_id' <<<"$first")
second_registration=$(jq -r '.result.registrations[0].registration_id' <<<"$second")
test "$first_watch" = "$second_watch"
test "$first_registration" = "$second_registration"

list_request=$(jq -nc --arg workspace "$workspace/store" '{
  action: "list",
  workspace: $workspace
}')
list_response=$($statewright internet-watch "$list_request")
jq -e --arg watch "$first_watch" \
  '.ok == true and (.result.active_watch_ids | index($watch)) != null' \
  <<<"$list_response" >/dev/null
