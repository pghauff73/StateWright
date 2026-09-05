#!/usr/bin/env bash
set -euo pipefail
benchmark=$1
resources=$2
fixture_root=$(mktemp -d /tmp/statewright-history-smoke.XXXXXX)
trap 'rm -rf "$fixture_root"' EXIT
"$benchmark" --stored-root "$fixture_root/history" --resources "$resources" 2 3 >"$fixture_root/result.json"
jq -e '
  [.results[].records] == [15,22] and
  [.results[].fetch_receipts] == [2,3] and
  all(.results[]; .repetitions == 3 and .selected_actions == 0 and
      .open_read_plan_milliseconds.median >= .planning_milliseconds.median and
      .stored_objects > .records and .stored_events >= .stored_objects)
' "$fixture_root/result.json" >/dev/null
if "$benchmark" --stored-root "$fixture_root/history" --resources "$resources" 2; then
  printf 'existing history was unexpectedly reused\n' >&2
  exit 1
fi
if "$benchmark" --stored-root "$fixture_root/invalid" --resources "$resources" 3 2; then
  printf 'decreasing history sizes were unexpectedly accepted\n' >&2
  exit 1
fi
test ! -e "$fixture_root/invalid"
