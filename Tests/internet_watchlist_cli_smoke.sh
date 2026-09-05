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
registry_path="$(dirname "$0")/../resources/watchlists/internet/source-groups-v1.json"
registry_sha=$($statewright hash-json "$(cat "$registry_path")")
policy_preview=$($statewright internet-watchlist "$(jq -nc --arg manifest "$manifest" '{
  action: "register", manifest_path: $manifest, dry_run: true, eligible_only: false
}')")
policy_id=$(jq -er '.result.registrations[0].source_policy_id' <<<"$policy_preview")
report_material=$(jq -nc \
  --arg manifest_sha "$manifest_sha" \
  --arg registry_sha "$registry_sha" \
  --arg policy_id "$policy_id" \
  --arg entry_sha "$entry_sha" '{
    checked_at: "2026-09-04T12:00:00Z",
    manifest_sha256: $manifest_sha,
    source_registry_sha256: $registry_sha,
    results: [{
      blocking_reasons: [],
      canonical_url: "https://www.w3.org/TR/rdf-canon/",
      eligible: true,
      entry_name: "w3c-rdf-canon",
      entry_sha256: $entry_sha,
      source_policy_id: $policy_id,
      status: "PREFLIGHT_ELIGIBLE",
      content_type: "text/html",
      final_url: "https://www.w3.org/TR/rdf-canon/",
      http_status: 200,
      provider_identity: "watchlist-cli-smoke-v1",
      resolved_addresses: ["93.184.216.34"],
      redirect_chain: [],
      compressed_bytes: 100,
      decompressed_bytes: 100,
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
    enable_eligible: false
  }')
dry_run_request=$(jq -c '. + {dry_run: true}' <<<"$register_request")
dry_run=$($statewright internet-watchlist "$dry_run_request")
jq -e '.ok == true and .result.dry_run == true and (.result.registrations | length) == 1' <<<"$dry_run" >/dev/null
test ! -e "$workspace/store"

first=$($statewright internet-watchlist "$register_request")
second=$($statewright internet-watchlist "$register_request")
jq -e '.ok == true and .result.registrations[0].eligibility_status == "REGISTERED_DISABLED"' <<<"$first" >/dev/null
first_watch=$(jq -r '.result.registrations[0].watch_id' <<<"$first")
second_watch=$(jq -r '.result.registrations[0].watch_id' <<<"$second")
first_registration=$(jq -r '.result.registrations[0].registration_id' <<<"$first")
second_registration=$(jq -r '.result.registrations[0].registration_id' <<<"$second")
test "$first_watch" = "$second_watch"
test "$first_registration" = "$second_registration"

enable_request=$(jq -nc --arg workspace "$workspace/store" --arg watch "$first_watch" '{
  workspace: $workspace, action: "enable", watch_id: $watch
}')
enabled=$($statewright internet-watch "$enable_request")
jq -e '.ok == true and .result.watch.enabled and (.result.registration_ids | length) == 1' <<<"$enabled" >/dev/null
enabled_watch=$(jq -er '.result.watch_id' <<<"$enabled")
carried_registration=$(jq -er '.result.registration_ids[0]' <<<"$enabled")
carried=$($statewright internet-source "$(jq -nc --arg workspace "$workspace/store" --arg id "$carried_registration" '{
  workspace: $workspace, action: "get", object_id: $id
}')")
jq -e --arg watch "$enabled_watch" --arg predecessor "$first_registration" \
  '.ok and .result.payload.watch_id == $watch and
   .result.payload.predecessor_registration_id == $predecessor and
   .result.payload.license_status == "verified" and
   .result.payload.extraction_strategy == "w3c-specification" and
   .result.payload.evidence_independence_group == "w3c"' <<<"$carried" >/dev/null

# Source changes must not borrow the previous page's eligibility.
changed_request=$(jq -nc --arg workspace "$workspace/store" --arg watch "$enabled_watch" '{
  workspace: $workspace, action: "supersede", watch_id: $watch,
  canonical_url: "https://www.w3.org/TR/json-ld11-api/"
}')
if changed=$($statewright internet-watch "$changed_request"); then
  printf 'source mutation unexpectedly reused eligible provenance: %s\n' "$changed" >&2
  exit 1
fi
jq -e '.ok == false' <<<"$changed" >/dev/null

list_request=$(jq -nc --arg workspace "$workspace/store" '{
  action: "list",
  workspace: $workspace
}')
list_response=$($statewright internet-watch "$list_request")
jq -e --arg watch "$enabled_watch" \
  '.ok == true and (.result.active_watch_ids | index($watch)) != null' \
  <<<"$list_response" >/dev/null

# Hash-valid imported reports are still denied when license declarations or
# registry/policy bindings no longer permit their claimed eligibility.
for license_status in prohibited review-required; do
  rejected_manifest=$(jq -c --arg status "$license_status" '.watches[0].license.status = $status' "$manifest")
  rejected_manifest_sha=$($statewright hash-json "$rejected_manifest")
  rejected_entry_sha=$($statewright hash-json "$(jq -c '.watches[0]' <<<"$rejected_manifest")")
  rejected_report=$(jq -c --arg manifest_sha "$rejected_manifest_sha" --arg entry_sha "$rejected_entry_sha" \
    '.manifest_sha256 = $manifest_sha | .results[0].entry_sha256 = $entry_sha' <<<"$report_material")
  rejected_signature=$($statewright hash-json "$rejected_report")
  rejected_report=$(jq -c --arg signature "$rejected_signature" '. + {report_signature: $signature}' <<<"$rejected_report")
  rejected_request=$(jq -nc --argjson manifest "$rejected_manifest" --argjson report "$rejected_report" '{
    action: "register", manifest: $manifest, preflight_report: $report,
    current_timestamp: "2026-09-04T12:00:00Z", enable_eligible: true, dry_run: true
  }')
  if rejected=$($statewright internet-watchlist "$rejected_request"); then
    printf 'unverified license unexpectedly became eligible: %s\n' "$rejected" >&2
    exit 1
  fi
  jq -e '.ok == false' <<<"$rejected" >/dev/null
done

for binding in source_registry_sha256 source_policy_id; do
  invalid_report=$(jq -c --arg binding "$binding" \
    'if $binding == "source_registry_sha256" then .source_registry_sha256 = "changed"
     else .results[0].source_policy_id = "changed" end' <<<"$report_material")
  invalid_signature=$($statewright hash-json "$invalid_report")
  invalid_report=$(jq -c --arg signature "$invalid_signature" '. + {report_signature: $signature}' <<<"$invalid_report")
  invalid_request=$(jq -c --argjson report "$invalid_report" \
    'del(.preflight_report_path) | . + {preflight_report: $report, dry_run: true}' <<<"$register_request")
  if invalid=$($statewright internet-watchlist "$invalid_request"); then
    printf 'changed preflight binding unexpectedly accepted: %s\n' "$invalid" >&2
    exit 1
  fi
  jq -e '.ok == false' <<<"$invalid" >/dev/null
done
