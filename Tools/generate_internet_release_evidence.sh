#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
qualification_date=${STATEWRIGHT_QUALIFICATION_DATE:-$(date +%Y-%m-%d)}
output=${1:-"$root/build/release-evidence/internet-release-$qualification_date"}

if [[ -e $output ]]; then
  printf 'refusing to overwrite existing internet evidence bundle: %s\n' \
    "$output" >&2
  exit 2
fi

mkdir -p "$output/logs" "$output/packages" "$output/manifests"

"$root/Tools/verify_release_inputs.sh" \
  >"$output/logs/release-inputs.log" 2>&1

for preset in developer sanitizer release; do
  cmake --preset "$preset" -S "$root" \
    >"$output/logs/configure-$preset.log" 2>&1
  cmake --build --preset "$preset" -j2 \
    >"$output/logs/build-$preset.log" 2>&1
  ctest --preset "$preset" --output-on-failure \
    >"$output/logs/ctest-$preset.log" 2>&1
done

"$root/Tests/internet_cli_smoke.sh" \
  "$root/build/release/statewright" \
  "$root/build/release/Tests/statewright_internet_fixture_server" \
  "$root/resources/fixtures/internet/identity-v1.json" \
  >"$output/logs/internet-cli-smoke.log" 2>&1

"$root/Tests/saa_internet_howto_smoke.sh" \
  "$root/docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md" \
  "$root/build/release/statewright" \
  "$root/build/release/Tests/statewright_internet_fixture_server" \
  "$root/resources/fixtures/internet/identity-v1.json" \
  >"$output/logs/saa-internet-howto-smoke.log" 2>&1

approval_workspace=$(mktemp -d)
trap 'rm -rf "$approval_workspace"' EXIT
if "$root/build/release/statewright" internet-improvement \
  "{\"workspace\":\"$approval_workspace\",\"action\":\"approve\"}" \
  >"$output/logs/internet-approve-rejection.log" 2>&1; then
  printf 'internet-improvement unexpectedly accepted approve\n' >&2
  exit 1
fi
grep -q 'unsupported internet-improvement action' \
  "$output/logs/internet-approve-rejection.log"

if grep -Eq '^#!.*python|^[[:space:]]*(env[[:space:]]+)?python(3)?([[:space:]]|$)' \
  "$root/Tests/internet_cli_smoke.sh" \
  "$root/Tests/saa_internet_howto_smoke.sh"; then
  printf 'qualified internet fixture path references a Python runtime\n' >&2
  exit 1
fi

cpack --config "$root/build/release/CPackConfig.cmake" -G TGZ \
  -B "$output/packages" >"$output/logs/cpack.log" 2>&1

"$root/build/release/statewright" version --json \
  >"$output/build-identity.json"

{
  printf 'generated_at=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'qualification_date=%s\n' "$qualification_date"
  printf 'cmake=%s\n' "$(cmake --version | head -n 1)"
  printf 'compiler=%s\n' "$(c++ --version | head -n 1)"
  printf 'openssl=%s\n' "$(openssl version)"
  printf 'curl=%s\n' "$(curl --version | head -n 1)"
  printf 'sqlite=%s\n' \
    "$(pkg-config --modversion sqlite3 2>/dev/null || printf unknown)"
  printf 'gmp=%s\n' \
    "$(pkg-config --modversion gmp 2>/dev/null || printf unknown)"
  printf 'gmpxx=%s\n' \
    "$(pkg-config --modversion gmpxx 2>/dev/null || printf unknown)"
} >"$output/toolchain.txt"

cp "$root/third_party/manifest.lock.json" "$output/dependency-lock.json"
cp "$root/resources/manifest.sha256" "$output/resource-manifest.sha256"
cp "$root/contracts/oracle/source.lock.json" "$output/oracle-source-lock.json"
cp "$root/contracts/oracle/source_manifest.sha256" \
  "$output/oracle-source-manifest.sha256"
cp "$root/docs/RESIDUAL_RISKS.md" "$output/RESIDUAL_RISKS.md"
cp "$root/docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md" \
  "$output/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md"
cp "$root/resources/fixtures/internet/identity-v1.json" \
  "$output/internet-fixture-metadata.json"
cp "$root/resources/policies/internet/default-source-policy-v1.json" \
  "$output/default-source-policy-v1.json"
cp "$root/resources/policies/internet/default-promotion-policy-v1.json" \
  "$output/default-promotion-policy-v1.json"

(
  cd "$root"
  find contracts/migrations -maxdepth 1 -type f -name '*.json' -print0 \
    | sort -z \
    | xargs -0 sha256sum
) >"$output/manifests/migrations.sha256"

(
  cd "$root"
  find Apps Core Tests Tools cmake contracts docs resources third_party \
       -type f -print0 \
    | sort -z \
    | xargs -0 sha256sum
  sha256sum CMakeLists.txt CMakePresets.json IMPLEMENTATION_PLAN.md README.md
) >"$output/manifests/target-source.sha256"

find "$output/packages" -maxdepth 1 -type f -print0 \
  | sort -z \
  | xargs -0 -r sha256sum >"$output/manifests/packages.sha256"

cat >"$output/internet-qualification-status.json" <<EOF
{
  "schema_version": 1,
  "generated_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "qualification_date": "$qualification_date",
  "qualification_status": "QUALIFIED_FIXTURE_SCOPE",
  "saa_internet_knowledge_lifecycle": {
    "human_approval_required": false,
    "approval_operation_present": false,
    "approval_records_created": 0,
    "autonomous_promotion_verified": true,
    "autonomous_demotion_verified": true,
    "previous_preference_restoration_verified": true
  },
  "authority": {
    "egcf_execution_authority_expanded": false,
    "automatic_c3_or_c5_authority_granted": false,
    "production_cutover_executed": false,
    "release_approved": false
  },
  "test_environment": {
    "live_internet_used": false,
    "python_runtime_required": false,
    "downloaded_code_executed": false,
    "howto_validated": true,
    "persistent_acquisition_writers": "ISOLATED_EPHEMERAL_WORKSPACES"
  },
  "qualified_scope": {
    "platform": "Linux/POSIX",
    "experiment_ir": ["IDENTITY", "CONST"],
    "probationary_canonical_admission": ["IDENTITY"],
    "scheduler": "LOCAL_SINGLE_HOST"
  }
}
EOF

(
  cd "$output"
  find . -type f ! -name manifest.sha256 -print0 \
    | sort -z \
    | xargs -0 sha256sum
) >"$output/manifest.sha256"

printf 'internet release evidence written to %s\n' "$output"
