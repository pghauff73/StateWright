#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
preset=${1:-release}
output=${2:-"$root/build/release-evidence/$preset"}

if [[ -e $output ]]; then
  printf 'refusing to overwrite existing evidence bundle: %s\n' "$output" >&2
  exit 2
fi

mkdir -p "$output/logs" "$output/packages"

"$root/Tools/verify_release_inputs.sh" \
  >"$output/logs/release-inputs.log" 2>&1
cmake --preset "$preset" -S "$root" \
  >"$output/logs/configure.log" 2>&1
cmake --build --preset "$preset" -j2 \
  >"$output/logs/build.log" 2>&1
ctest --preset "$preset" --output-on-failure \
  >"$output/logs/ctest.log" 2>&1
cpack --config "$root/build/$preset/CPackConfig.cmake" -G TGZ \
  -B "$output/packages" >"$output/logs/cpack.log" 2>&1

"$root/build/$preset/statewright" version --json \
  >"$output/build-identity.json"

{
  printf 'generated_at=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'cmake=%s\n' "$(cmake --version | head -n 1)"
  printf 'compiler=%s\n' "$(c++ --version | head -n 1)"
  printf 'openssl=%s\n' "$(openssl version)"
  printf 'sqlite=%s\n' "$(pkg-config --modversion sqlite3 2>/dev/null || printf unknown)"
  printf 'gmp=%s\n' "$(pkg-config --modversion gmp 2>/dev/null || printf unknown)"
  printf 'gmpxx=%s\n' "$(pkg-config --modversion gmpxx 2>/dev/null || printf unknown)"
} >"$output/toolchain.txt"

cp "$root/third_party/manifest.lock.json" "$output/dependency-lock.json"
cp "$root/resources/manifest.sha256" "$output/resource-manifest.sha256"
cp "$root/contracts/oracle/source.lock.json" "$output/oracle-source-lock.json"
cp "$root/contracts/oracle/source_manifest.sha256" \
  "$output/oracle-source-manifest.sha256"
cp "$root/docs/RESIDUAL_RISKS.md" "$output/RESIDUAL_RISKS.md"

(
  cd "$root"
  find Apps Core Tests Tools cmake contracts docs resources third_party \
       -type f -print0 \
    | sort -z \
    | xargs -0 sha256sum
  sha256sum CMakeLists.txt CMakePresets.json IMPLEMENTATION_PLAN.md README.md
) >"$output/target-source-manifest.sha256"

find "$output/packages" -maxdepth 1 -type f -print0 \
  | sort -z \
  | xargs -0 -r sha256sum >"$output/package-manifest.sha256"

cat >"$output/release-status.json" <<EOF
{
  "schema_version": 1,
  "generated_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "preset": "$preset",
  "qualification_status": "IMPLEMENTATION_CHECKPOINT",
  "release_approved": false,
  "human_approval": "PENDING",
  "cutover": {
    "core": "NOT_EXECUTED",
    "reasoning": "NOT_EXECUTED",
    "saa": "NOT_EXECUTED",
    "egcf": "NOT_EXECUTED"
  },
  "python_owner_retirement_verified": false,
  "automatic_authority_granted": false
}
EOF

(
  cd "$output"
  find . -type f ! -name manifest.sha256 -print0 \
    | sort -z \
    | xargs -0 sha256sum
) >"$output/manifest.sha256"

printf 'release evidence written to %s\n' "$output"

