#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
budget_seconds=${STATEWRIGHT_SANITIZER_BUDGET_SECONDS:-60}
if [[ ! $budget_seconds =~ ^[1-9][0-9]*$ ]]; then
  printf 'STATEWRIGHT_SANITIZER_BUDGET_SECONDS must be a positive integer\n' >&2
  exit 2
fi
budget_milliseconds=$((budget_seconds * 1000))
start=$(date +%s%N)
set +e
(
  cd "$root"
  ctest --preset sanitizer --output-on-failure "$@"
)
ctest_status=$?
set -e
end=$(date +%s%N)
elapsed_milliseconds=$(((end - start) / 1000000))

printf 'sanitizer_elapsed_ms=%d\n' "$elapsed_milliseconds"
printf 'sanitizer_budget_ms=%d\n' "$budget_milliseconds"
if ((ctest_status != 0)); then
  exit "$ctest_status"
fi
if ((elapsed_milliseconds >= budget_milliseconds)); then
  printf 'sanitizer qualification exceeded the strict %d-second budget\n' \
    "$budget_seconds" >&2
  exit 1
fi
printf 'sanitizer qualification passed under %d seconds\n' "$budget_seconds"
