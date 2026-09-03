#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
lock_file="$root/contracts/oracle/source.lock.json"
oracle_repo=${STATEWRIGHT_ORACLE_REPOSITORY:-$(jq -r '.oracle_repository' "$lock_file")}
oracle_commit=$(jq -r '.oracle_commit' "$lock_file")
output="$root/contracts/fixtures/egcf-records-v1.json"

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
git -C "$oracle_repo" show "$oracle_commit:ourd/egcf/ids.py" >"$temporary/ids.py"
git -C "$oracle_repo" show "$oracle_commit:schemas/egcf-v1/objects.schema.json" \
  >"$temporary/objects.schema.json"

ORACLE_IDS="$temporary/ids.py" \
OBJECT_SCHEMA="$temporary/objects.schema.json" \
ORACLE_COMMIT="$oracle_commit" \
OUTPUT="$output" \
python - <<'PY'
import importlib.util
import json
import os
from pathlib import Path

spec = importlib.util.spec_from_file_location("statewright_oracle_ids", os.environ["ORACLE_IDS"])
ids = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(ids)

schema = json.loads(Path(os.environ["OBJECT_SCHEMA"]).read_text(encoding="utf-8"))
definitions = schema["$defs"]


def sample_value(value_schema, field_name="value"):
    if "$ref" in value_schema:
        prefix = "#/$defs/"
        reference = value_schema["$ref"]
        assert reference.startswith(prefix)
        return sample_object(definitions[reference[len(prefix):]])
    if "const" in value_schema:
        return value_schema["const"]
    expected = value_schema.get("type")
    if isinstance(expected, list):
        expected = expected[0]
    if expected == "object":
        return sample_object(value_schema)
    if expected == "array":
        item_schema = value_schema.get("items")
        return [] if not item_schema else [sample_value(item_schema, field_name + "-item")]
    if expected == "string":
        return field_name + "-value"
    if expected == "integer":
        return 1
    if expected == "number":
        return 1.25
    if expected == "boolean":
        return True
    if expected == "null":
        return None
    return None


def sample_object(object_schema):
    properties = object_schema.get("properties", {})
    return {
        name: sample_value(properties[name], name)
        for name in object_schema.get("required", [])
    }


cases = []
for object_type, object_schema in sorted(definitions.items()):
    payload = sample_object(object_schema)
    cases.append(
        {
            "object_type": object_type,
            "object_id": ids.typed_id(object_type, payload),
            "payload": payload,
        }
    )

result = {
    "schema_version": 1,
    "oracle_commit": os.environ["ORACLE_COMMIT"],
    "case_count": len(cases),
    "cases": cases,
}
output = Path(os.environ["OUTPUT"])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
PY

sha256sum "$output" | awk '{print $1}' >"$output.sha256"
printf 'Exported %s EGCF record fixtures from %s\n' \
  "$(jq '.case_count' "$output")" "$oracle_commit"
