#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

require_bare_checksum() {
  local payload=$1
  local checksum_file=$2
  local expected actual
  expected=$(tr -d '[:space:]' <"$checksum_file")
  actual=$(sha256sum "$payload" | awk '{print $1}')
  if [[ ! $expected =~ ^[0-9a-f]{64}$ ]] || [[ $actual != "$expected" ]]; then
    printf 'checksum mismatch: %s\n' "$payload" >&2
    return 1
  fi
  printf '%s: OK\n' "${payload#$root/}"
}

(
  cd "$root/resources"
  sha256sum -c manifest.sha256
)

require_bare_checksum \
  "$root/contracts/fixtures/foundation-v1.json" \
  "$root/contracts/fixtures/foundation-v1.json.sha256"
require_bare_checksum \
  "$root/contracts/fixtures/egcf-records-v1.json" \
  "$root/contracts/fixtures/egcf-records-v1.json.sha256"
require_bare_checksum \
  "$root/contracts/oracle/source_manifest.json" \
  "$root/contracts/oracle/source_manifest.sha256"

while IFS= read -r checksum_file; do
  (
    cd "$(dirname "$checksum_file")"
    sha256sum -c "$(basename "$checksum_file")"
  )
done < <(find "$root/resources/benchmarks" -type f -name '*.sha256' | sort)

oracle_commit=$(jq -r '.oracle_commit' "$root/contracts/oracle/source.lock.json")
cmake_commit=$(sed -n 's/^[[:space:]]*"\([0-9a-f]\{40\}\)"$/\1/p' \
  "$root/CMakeLists.txt" | head -n 1)
if [[ -z $oracle_commit || $oracle_commit != "$cmake_commit" ]]; then
  printf 'oracle commit mismatch between source lock and CMake\n' >&2
  exit 1
fi

frozen_schema_hash=$(jq -r '.frozen_contract_sha256' \
  "$root/contracts/migrations/0001-brain-feed-record-extension.json")
actual_schema_hash=$(sha256sum \
  "$root/resources/schemas/egcf-v1/objects.schema.json" | awk '{print $1}')
if [[ $frozen_schema_hash != "$actual_schema_hash" ]]; then
  printf 'frozen EGCF schema differs from its migration receipt\n' >&2
  exit 1
fi

jq -e '.schema_version == 1 and (.dependencies | length > 0)' \
  "$root/third_party/manifest.lock.json" >/dev/null
jq -e '.schema_version == 1 and .status == "IMPLEMENTED_NON_BREAKING_EXTENSION"' \
  "$root/contracts/migrations/0001-brain-feed-record-extension.json" >/dev/null

jq -e '
  . as $presets |
  ($presets.configurePresets[] | select(.name == "sanitizer") |
    .cacheVariables) as $cache |
  ($presets.testPresets[] | select(.name == "sanitizer")) as $test |
  $cache.CMAKE_BUILD_TYPE == "RelWithDebInfo" and
  $cache.STATEWRIGHT_ENABLE_SANITIZERS == "ON" and
  $cache.STATEWRIGHT_SANITIZER_TEST_SHARDS == "32" and
  ($cache.CMAKE_CXX_FLAGS_RELWITHDEBINFO | contains("-O2")) and
  ($cache.CMAKE_CXX_FLAGS_RELWITHDEBINFO | contains("-DNDEBUG") | not) and
  $test.execution.jobs == 12
' "$root/CMakePresets.json" >/dev/null

printf 'release inputs verified for oracle %s\n' "$oracle_commit"

