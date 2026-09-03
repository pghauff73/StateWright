#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
lock_file="$root/contracts/oracle/source.lock.json"
oracle_repo=${STATEWRIGHT_ORACLE_REPOSITORY:-$(jq -r '.oracle_repository' "$lock_file")}
oracle_commit=$(jq -r '.oracle_commit' "$lock_file")

git -C "$oracle_repo" cat-file -e "$oracle_commit^{commit}"

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
paths_file="$temporary/paths.txt"
entries_file="$temporary/entries.jsonl"

git -C "$oracle_repo" ls-tree -r --name-only "$oracle_commit" -- \
  ourd/reasoning \
  ourd/providers \
  ourd/egcf \
  ourd/context_budget.py \
  ourd/constants.py \
  ourd/errors.py \
  ourd/hypotheses.py \
  ourd/oiec.py \
  ourd/loop_control.py \
  ourd/models.py \
  ourd/authority.py \
  ourd/workspace.py \
  ourd/persistence.py \
  ourd/transactions.py \
  ourd/policy.py \
  ourd/cfel.py \
  algorithms/v1 \
  commands/v1 \
  workflows/v1 \
  schemas/egcf-v1 \
  schemas/providers \
  grammars/providers \
  benchmarks/reasoning \
  tools/run_reasoning_model_benchmark.py \
  OIEC_SR_V1_IMPLEMENTATION_PLAN.md \
  EGCFV1_IMPLEMENTATION_PLAN.md \
  docs/adr/0001-egcf-semantic-layer.md \
  docs/adr/0002-egcf-canonical-storage.md \
  docs/adr/0003-egcf-capability-authority.md \
  docs/adr/0004-egcf-eon-boundary.md \
  >"$paths_file"

git -C "$oracle_repo" ls-tree -r --name-only "$oracle_commit" -- tests \
  | grep -E '(^tests/reasoning/|^tests/providers/|tests/[^/]*(egcf|saa|reason|hypoth|provider)[^/]*\.py$|^tests/helpers\.py$)' \
  >>"$paths_file"

sort -u -o "$paths_file" "$paths_file"

while IFS= read -r path; do
  tree_entry=$(git -C "$oracle_repo" ls-tree "$oracle_commit" -- "$path")
  mode=${tree_entry%% *}
  remainder=${tree_entry#* }
  remainder=${remainder#* }
  blob=${remainder%%$'\t'*}
  size=$(git -C "$oracle_repo" cat-file -s "$blob")
  sha256=$(git -C "$oracle_repo" show "$oracle_commit:$path" | sha256sum | cut -d' ' -f1)
  jq -cn \
    --arg path "$path" \
    --arg mode "$mode" \
    --arg git_blob "$blob" \
    --arg sha256 "$sha256" \
    --argjson size "$size" \
    '{path:$path,mode:$mode,size:$size,git_blob:$git_blob,sha256:$sha256}' \
    >>"$entries_file"
done <"$paths_file"

jq -sS \
  --arg commit "$oracle_commit" \
  --arg repository "$oracle_repo" \
  '{schema_version:1,selected_on:"2026-09-02",oracle_commit:$commit,oracle_repository:$repository,working_tree_included:false,file_count:length,files:.}' \
  "$entries_file" >"$root/contracts/oracle/source_manifest.json"

sha256sum "$root/contracts/oracle/source_manifest.json" \
  | awk '{print $1}' >"$root/contracts/oracle/source_manifest.sha256"

rm -rf "$root/resources"
mkdir -p "$root/resources"
while IFS= read -r path; do
  case "$path" in
    algorithms/v1/*|commands/v1/*|workflows/v1/*|schemas/egcf-v1/*|schemas/providers/*|grammars/providers/*|benchmarks/reasoning/*)
      destination="$root/resources/$path"
      mkdir -p "$(dirname "$destination")"
      git -C "$oracle_repo" show "$oracle_commit:$path" >"$destination"
      ;;
  esac
done <"$paths_file"

(
  cd "$root/resources"
  find . -type f ! -name manifest.sha256 -print0 \
    | sort -z \
    | xargs -0 sha256sum \
    >manifest.sha256
)

printf 'Frozen %s files from %s\n' "$(wc -l <"$paths_file")" "$oracle_commit"
