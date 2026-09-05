#!/usr/bin/env bash
# One check / one import. Scheduling belongs to the app, not a shell polling loop.
# Exit 10: earlier batch is still incomplete. Exit 20: attention required.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
prior="$root/build/all-lanes-feed-20260904T224120Z"
output="$root/build/mathematics-project-import-v1"

if [[ -f "$output/integrity.json" ]]; then
  jq -e '.ok == true' "$output/integrity.json" >/dev/null
  jq -e 'length == 20 and all(.[]; .status == "QUARANTINED")' "$output/feed-results.json" >/dev/null
  printf '%s\n' 'COMPLETE: 20 mathematical acquisitions already fed to the project.'
  exit 0
fi
if [[ ! -f "$prior/integrity.json" ]]; then
  printf '%s\n' 'WAITING: earlier RFC/W3C batch has not published its final integrity record.'
  exit 10
fi
if ! jq -e '.ok == true' "$prior/integrity.json" >/dev/null ||
   ! jq -e 'length == 40' "$prior/feed-results.json" >/dev/null ||
   ! jq -e 'length == 40' "$prior/disabled.json" >/dev/null; then
  printf '%s\n' 'ATTENTION: earlier batch completion or watch cleanup is incomplete.' >&2
  exit 20
fi
if [[ -e "$output" ]]; then
  printf '%s\n' 'ATTENTION: import evidence exists without completion; inspect or resume its running process, do not start a duplicate.' >&2
  exit 20
fi
exec "$root/build/statewright_import_mathematical_acquisitions" \
  "$root/build/mathematics-pilot-store-v1" "$root" "$root/resources" \
  "$root/build/mathematics-pilot-evidence-v1" "$output"
