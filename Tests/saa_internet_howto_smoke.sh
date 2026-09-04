#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  printf 'usage: %s HOWTO STATEWRIGHT FIXTURE_SERVER FIXTURE_METADATA\n' \
    "$0" >&2
  exit 2
fi

howto=$1
statewright=$2
fixture_server=$3
fixture_metadata=$4
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$script_dir/.." && pwd)
blocks_dir=$(mktemp -d /tmp/statewright-saa-howto-blocks.XXXXXX)
cleanup() {
  rm -rf "$blocks_dir"
}
trap cleanup EXIT

for path in "$howto" "$statewright" "$fixture_server" "$fixture_metadata"; do
  if [[ ! -e $path ]]; then
    printf 'required HOWTO validation input is missing: %s\n' "$path" >&2
    exit 1
  fi
done

require_text() {
  local text=$1
  if ! grep -Fq -- "$text" "$howto"; then
    printf 'HOWTO is missing required text: %s\n' "$text" >&2
    exit 1
  fi
}

require_text '# SAA Persistent Internet Improvement HOWTO'
require_text '**Qualified implementation date:** 2026-09-03'
require_text '## 4. Five-Minute Qualified Fixture Run'
require_text '## 5. Manual Fixture Walkthrough'
require_text '## 6. Inspection and Control Recipes'
require_text '## 7. Bounded Real-Internet Operation'
require_text '## 8. Troubleshooting by Lifecycle Boundary'
require_text '## 10. Qualification and Evidence'
require_text '## 11. Qualified Limits'
require_text '## 12. Maintenance Traceability'

for operation in \
  internet-watch \
  internet-poll \
  internet-fetch \
  internet-source \
  internet-extract \
  internet-candidate \
  internet-improvement \
  internet-promotion-policy \
  internet-probation \
  internet-integrity; do
  require_text "$operation"
done

for action in \
  register supersede enable disable list get schedule select lease execute \
  assess explain migrate feed reason experiment-qualify policy-assess \
  probation-admit probation-select probation-observe advance status \
  plan run-once resume run-status explain-action protocol-register \
  source-assessment-input-register probation-observation-input-register \
  register-default rebuild verify; do
  require_text "$action"
done

require_text 'StateWright internet CLI smoke passed without Python or live internet'
require_text 'human_approval_required == false'
require_text 'The rejected action is an invariant check, not a step to add back.'
require_text 'Downloaded source code is never an acceptable substitute for internal SAA IR.'
require_text 'General EGCF'
require_text 'execution approvals remain a separate command-fabric concern.'
require_text 'projection.sqlite3'
require_text 'internet_records'
require_text 'internet_record_fts'
require_text 'IDENTITY'
require_text 'CONST'
require_text 'Tests/saa_internet_howto_smoke.sh'
require_text 'Tools/generate_internet_release_evidence.sh'
require_text 'Tests/internet_orchestrator_cli_smoke.sh'
require_text 'Tests/internet_orchestrator_fault_smoke.sh'
require_text 'does not claim a continuously running multi-process daemon'
require_text 'version --json` is the build-identity exception'

for referenced_path in \
  docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_PLAN.md \
  docs/SAA_INTERNET_IMPLEMENTATION_AUDIT.md \
  docs/RESIDUAL_RISKS.md \
  Apps/statewright/cli.cpp \
  Tests/internet_cli_smoke.sh \
  Tests/internet_orchestrator_cli_smoke.sh \
  Tests/internet_orchestrator_fault_smoke.sh \
  Tests/sources/internet_fixture_server.cpp \
  resources/fixtures/internet/identity-v1.json \
  resources/policies/internet/default-source-policy-v1.json \
  resources/policies/internet/default-promotion-policy-v1.json \
  Tools/generate_internet_release_evidence.sh; do
  if [[ ! -e $root/$referenced_path ]]; then
    printf 'HOWTO canonical reference is missing: %s\n' "$referenced_path" >&2
    exit 1
  fi
done

awk -v directory="$blocks_dir" '
  /^```bash[[:space:]]*$/ {
    in_block = 1
    count += 1
    file = sprintf("%s/block-%03d.sh", directory, count)
    next
  }
  /^```[[:space:]]*$/ && in_block {
    close(file)
    in_block = 0
    next
  }
  in_block { print >> file }
  END {
    if (in_block) exit 2
    print count > (directory "/count")
  }
' "$howto"

block_count=$(<"$blocks_dir/count")
if [[ $block_count -lt 10 ]]; then
  printf 'HOWTO contains too few executable Bash blocks: %s\n' \
    "$block_count" >&2
  exit 1
fi
for block in "$blocks_dir"/block-*.sh; do
  bash -n "$block"
  if grep -Eq '^#!.*python|^[[:space:]]*(env[[:space:]]+)?python(3)?([[:space:]]|$)' \
    "$block"; then
    printf 'HOWTO Bash block invokes Python: %s\n' "$block" >&2
    exit 1
  fi
done

bash -n "$script_dir/internet_cli_smoke.sh"
"$script_dir/internet_cli_smoke.sh" \
  "$statewright" "$fixture_server" "$fixture_metadata"

printf 'StateWright SAA internet HOWTO validation passed\n'
