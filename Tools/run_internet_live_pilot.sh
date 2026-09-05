#!/usr/bin/env bash
# Explicit, bounded live network pilot. Never starts a timer or supervisor.
set -euo pipefail
statewright=$(realpath "${1:?usage: bash Tools/run_internet_live_pilot.sh BINARY NEW_OUTPUT_DIRECTORY}")
output=$(realpath -m "${2:?provide a new evidence directory}")
if [[ -e "$output" ]]; then
  printf 'refusing to overwrite evidence directory: %s\n' "$output" >&2
  exit 1
fi
mkdir -p "$output"
workspace="$output/store"
call() {
  local command=$1 request=$2 destination=$3
  "$statewright" "$command" "$request" >"$destination"
  jq -e '.ok == true' "$destination" >/dev/null
}
cleanup() {
  local status=$? watch
  trap - EXIT
  if [[ -f "$output/registration.json" ]]; then
    while IFS= read -r watch; do
      "$statewright" internet-watch "$(jq -nc --arg workspace "$workspace" --arg id "$watch" \
        '{workspace:$workspace,action:"disable",watch_id:$id}')" >>"$output/disabled.jsonl" || status=1
    done < <(jq -r '.result.registrations[].watch_id' "$output/registration.json")
  fi
  if [[ -d "$workspace" ]]; then
    call internet-improvement "$(jq -nc --arg workspace "$workspace" \
      '{workspace:$workspace,action:"metrics"}')" "$output/final-metrics.json" || status=1
    call internet-integrity "$(jq -nc --arg workspace "$workspace" \
      '{workspace:$workspace,action:"verify"}')" "$output/integrity.json" || status=1
  fi
  exit "$status"
}
trap cleanup EXIT
now=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
call internet-watchlist "$(jq -nc --arg output "$output/watchlist.json" \
  '{action:"create",template:"rfc-number",numbers:[5869,6234,7693],enabled:true,
    watchlist_version:"saa-bounded-three-rfc-pilot-v1",output_path:$output}')" "$output/create.json"
call internet-watchlist "$(jq -nc --arg manifest "$output/watchlist.json" --arg now "$now" \
  --arg output "$output/preflight.json" \
  '{action:"preflight",manifest_path:$manifest,checked_at:$now,output_path:$output}')" "$output/preflight-envelope.json"
now=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
call internet-watchlist "$(jq -nc --arg workspace "$workspace" --arg manifest "$output/watchlist.json" \
  --arg report "$output/preflight.json" --arg now "$now" \
  '{action:"register",workspace:$workspace,manifest_path:$manifest,preflight_report_path:$report,
    current_timestamp:$now,enable_eligible:true}')" "$output/registration.json"
jq -e '(.result.registrations | length) > 0' "$output/registration.json" >/dev/null
call internet-improvement "$(jq -nc --arg workspace "$workspace" \
  '{workspace:$workspace,action:"metrics"}')" "$output/baseline-metrics.json"

# Two explicit sampling passes, not a permanent polling-interval change.
for pass in 1 2; do
  now=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
  deadline=$(date -u -d '+10 minutes' '+%Y-%m-%dT%H:%M:%SZ')
  call internet-poll "$(jq -nc --arg workspace "$workspace" --arg now "$now" --arg deadline "$deadline" \
    '{workspace:$workspace,action:"schedule",scheduled_interval:$now,earliest_start:$now,
      deadline:$deadline,retry_ceiling:0}')" "$output/pass-$pass-schedule.json"
  for cycle in $(seq 1 20); do
    now=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
    expiry=$(date -u -d '+90 seconds' '+%Y-%m-%dT%H:%M:%SZ')
    call internet-improvement "$(jq -nc --arg workspace "$workspace" --arg now "$now" \
      --arg expiry "$expiry" --arg cycle "pilot-$pass-$cycle" \
      '{workspace:$workspace,action:"run-once",current_timestamp:$now,cycle_key:$cycle,
        worker_id:"bounded-live-pilot",action_lease_expires_at:$expiry,fetch_lease_expires_at:$expiry,
        policy:{maximum_actions:1,maximum_provider_calls:0,action_deadline:$expiry,
          enabled_action_kinds:["EXECUTE_FETCH","ASSESS_SOURCE","EXTRACT_SNAPSHOT","FEED_EXTRACTION"]}}')" \
      "$output/pass-$pass-cycle-$cycle.json"
    status=$(jq -r '.result.status' "$output/pass-$pass-cycle-$cycle.json")
    printf 'pass %s cycle %s: %s\n' "$pass" "$cycle" "$status"
    if [[ "$status" == NO_ELIGIBLE_WORK ]]; then break; fi
    if [[ "$status" != COMPLETED ]]; then exit 1; fi
    if (( cycle == 20 )); then
      printf 'bounded cycle limit reached with unfinished work\n' >&2
      exit 1
    fi
  done
  call internet-improvement "$(jq -nc --arg workspace "$workspace" \
    '{workspace:$workspace,action:"metrics"}')" "$output/pass-$pass-metrics.json"
done
for kind in receipts snapshots assessments extractions; do
  call internet-source "$(jq -nc --arg workspace "$workspace" --arg kind "$kind" \
    '{workspace:$workspace,action:"list",kind:$kind}')" "$output/$kind.json"
done
printf 'Pilot evidence: %s\n' "$output"
