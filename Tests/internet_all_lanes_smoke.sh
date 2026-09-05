#!/usr/bin/env bash
set -euo pipefail
statewright=$1
feeder=$2
resources=$3
generator=$4
test_root=$(mktemp -d /tmp/statewright-all-lanes.XXXXXX)
trap 'rm -rf "$test_root"' EXIT
bash "$generator" "$statewright" "$test_root/all.json" >/dev/null
jq -e '(.watches|length)==100 and ([.watches[].canonical_url]|unique|length)==100 and
  ([.watches|group_by(.source_group)[]|length] == [10,20,20,10,20,20]) and
  all(.watches[]; .source_group != "nist-dlmf")' "$test_root/all.json" >/dev/null
# All entries in this offline fixture must be blocked before HTTP is invoked.
jq '.watches = [.watches[] | select(.source_group == "crossref")][0:2]' \
  "$test_root/all.json" >"$test_root/blocked.json"
"$feeder" "$test_root/store" "$resources" "$test_root/blocked.json" "$test_root/evidence"
jq -e 'length == 2 and all(.[]; .status == "QUARANTINED")' "$test_root/evidence/registrations.json" >/dev/null
jq -e '.watches.current == 2 and .watches.enabled == 0 and .fetches.successful == 0' \
  "$test_root/evidence/final-metrics.json" >/dev/null
jq -e '.ok' "$test_root/evidence/integrity.json" >/dev/null
# Neither existing evidence nor pre-existing watches may be adopted/disabled.
if "$feeder" "$test_root/store" "$resources" "$test_root/blocked.json" "$test_root/evidence"; then exit 1; fi
if "$feeder" "$test_root/store" "$resources" "$test_root/blocked.json" "$test_root/repeated"; then exit 1; fi
