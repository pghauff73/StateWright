# SAA Persistent Internet Improvement HOWTO

**Qualified implementation date:** 2026-09-03

**Qualified scope:** Linux/POSIX, local single-host scheduling, native loopback
fixture, unauthenticated HTTP/HTTPS acquisition, exact scalar SAA experiments,
and deterministic autonomous promotion and demotion

## 1. What This HOWTO Operates

StateWright's SAA persistent internet improvement subsystem turns a bounded
internet source into immutable evidence and, when every deterministic gate
passes, a probationary or canonical Searchable Algebra of Algorithms (SAA)
entry.

The implemented lifecycle is:

```text
source policy + watch
        |
        v
scheduled job -> lease -> bounded fetch -> immutable snapshot
        |
        v
source assessment -> deterministic extraction -> BrainFeedProcessor
        |
        v
existing-knowledge-first retrieval -> candidate -> advisory OIEC-SR reasoning
        |
        v
exact internal experiment -> autonomous policy assessment
        |
        v
PROBATIONARY_CANONICAL -> bounded canary observations
        |                                  |
        v                                  v
    CANONICAL                         regression
                                           |
                                           v
                                       DEMOTED
                                           |
                                           v
                              previous preference restored
```

The internet is an evidence source, not an execution authority. Downloaded
text, markup, source code, binaries, metadata, and embedded instructions are
never executed. OIEC-SR and external model providers are advisory only.

Normal SAA internet promotion has no human approval operation and creates no
approval record. This does **not** remove or bypass the separate general EGCF
approval boundary for exact-plan C3 command execution, and it does not approve
production ownership cutover.

## 2. Authoritative State and Projections

Each request must contain the intended `workspace`. StateWright resolves that
path and stores authoritative EGCF state under:

```text
<workspace>/.ourd-agent/egcf/
  objects/sha256/<prefix>/<digest>.json
  artifacts/sha256/<prefix>/<digest>
  events.jsonl
  projection.sqlite3
```

The content-addressed objects, artifacts, and append-only event chain are
authoritative. `projection.sqlite3`, including `internet_records` and
`internet_record_fts`, is derived and rebuildable.

Use a disposable workspace for tutorials. Supplying `root` instead of
`workspace`, omitting `workspace`, or pointing it at the repository can create
`.ourd-agent` state in an unintended directory.

## 3. Prerequisites

The qualified path requires:

- Linux/POSIX;
- CMake 3.28 or later and Ninja;
- a C++20 compiler;
- OpenSSL, SQLite, GMP/GMPXX, and libcurl development packages;
- Bash, `jq`, `sha256sum`, and ordinary POSIX process tools;
- no Python runtime;
- no live internet for the default fixture path.

Configure and build:

```bash
cmake --preset release
cmake --build --preset release -j2
```

Set reusable paths:

```bash
export STATEWRIGHT="$PWD/build/release/statewright"
export STATEWRIGHT_FIXTURE_SERVER="$PWD/build/release/Tests/statewright_internet_fixture_server"
export STATEWRIGHT_FIXTURE_METADATA="$PWD/resources/fixtures/internet/identity-v1.json"
```

Verify the binary and manifest-bound resources:

```bash
"$STATEWRIGHT" version --json \
  | jq -e 'has("statewright_version") and has("oracle_commit")'
Tools/verify_release_inputs.sh
```

All native JSON operations return a `statewright.cli.v1` envelope. Successful
commands have `.ok == true`; operation-specific data is under `.result`.
`version --json` is the build-identity exception and returns its fields directly.

### 3.1 Stable Internet Command Inventory

| Operation | Supported actions |
| --- | --- |
| `internet-watch` | `register`, `supersede`, `enable`, `disable`, `list`, `get` |
| `internet-poll` | `schedule`, `select`, `lease`, `list` |
| `internet-fetch` | `execute`, `list` |
| `internet-source` | `assess`, `list`, `get` |
| `internet-extract` | `execute`, `list`, `proposals` |
| `internet-candidate` | `list`, `get`, `explain`, `migrate` |
| `internet-improvement` | `status`, `metrics`, `plan`, `run-once`, `resume`, `run-status`, `explain-action`, `advance`, `protocol-register`, `source-assessment-input-register`, `probation-observation-input-register`, `feed`, `reason`, `experiment-qualify`, `policy-assess`, `probation-admit`, `probation-select`, `probation-observe` |
| `internet-promotion-policy` | `list`, `get`, `register`, `register-default`, `assess` |
| `internet-probation` | `list`, `admit`, `select`, `observe` |
| `internet-integrity` | `verify`, `rebuild` |

Defaults vary by operation: most inspection surfaces default to `list` or
`status`, `internet-integrity` defaults to `verify`, and `internet-extract`
defaults to `execute`. The HOWTO uses explicit actions so every read and state
transition remains visible.

## 4. Five-Minute Qualified Fixture Run

Run the complete supported fixture lifecycle with one command:

```bash
Tests/internet_cli_smoke.sh \
  "$STATEWRIGHT" \
  "$STATEWRIGHT_FIXTURE_SERVER" \
  "$STATEWRIGHT_FIXTURE_METADATA"
```

Expected final line:

```text
StateWright internet CLI smoke passed without Python or live internet
```

This fixture proves:

- immutable source-policy and watch registration;
- deterministic scheduling and exclusive lease acquisition;
- native bounded HTTP capture and exact snapshot hashing;
- source assessment and deterministic extraction;
- ordinary brain-feed ingestion and candidate creation;
- advisory reasoning with no authoritative model state;
- exact `CONST` baseline versus `IDENTITY` candidate experiments;
- policy-qualified promotion with `human_approval_required == false`;
- probationary admission and bounded canary selection;
- automatic promotion after four observations across two windows;
- automatic demotion after a benchmark regression;
- restoration of the previous canonical preference;
- projection rebuild and integrity verification;
- zero approval records and rejection of an `approve` action.

It proves the packaged fixture scope only. It is not a general web-crawling or
production-cutover qualification.

## 5. Manual Fixture Walkthrough

Run the blocks in this section in the same Bash shell. They mirror the canonical
flow in `Tests/internet_cli_smoke.sh` while exposing each input and produced
record identity.

### 5.1 Create an Isolated Workspace

```bash
export SAA_WORKSPACE="$(mktemp -d /tmp/statewright-saa-howto.XXXXXX)"
export SAA_PORT_FILE="$SAA_WORKSPACE/fixture-port"
SAA_SERVER_PID=

cleanup_saa_howto() {
  if [[ -n ${SAA_SERVER_PID:-} ]]; then
    kill "$SAA_SERVER_PID" 2>/dev/null || true
    wait "$SAA_SERVER_PID" 2>/dev/null || true
  fi
  rm -rf "$SAA_WORKSPACE"
}
trap cleanup_saa_howto EXIT

"$STATEWRIGHT_FIXTURE_SERVER" "$SAA_PORT_FILE" &
SAA_SERVER_PID=$!
for _ in $(seq 1 100); do
  [[ -s $SAA_PORT_FILE ]] && break
  kill -0 "$SAA_SERVER_PID" 2>/dev/null || {
    printf 'fixture server exited before publishing its port\n' >&2
    exit 1
  }
  sleep 0.02
done
[[ -s $SAA_PORT_FILE ]]
export SAA_PORT="$(tr -d '[:space:]' <"$SAA_PORT_FILE")"
```

Define a helper that rejects failed CLI envelopes:

```bash
saa_run() {
  local operation=$1
  local request=$2
  local output
  output=$("$STATEWRIGHT" "$operation" "$request")
  jq -e '.ok == true' <<<"$output" >/dev/null
  printf '%s\n' "$output"
}
```

### 5.2 Register a Source Policy and Watch

The loopback policy is test-only. It deliberately disables TLS and permits the
fixture port; do not reuse it for public sources.

```bash
source_policy=$(jq -cn --argjson port "$SAA_PORT" '{
  schema_version: 1,
  policy_version: "statewright-internet-source-policy-v1",
  allowed_schemes: ["http"],
  allowed_ports: [$port],
  accepted_mime_types: ["text/plain"],
  maximum_redirects: 1,
  maximum_header_bytes: 4096,
  maximum_response_bytes: 4096,
  maximum_decompressed_bytes: 4096,
  connect_timeout_seconds: 1,
  request_timeout_seconds: 2,
  require_tls_verification: false,
  allow_loopback_for_tests: true,
  require_robots_permission: true,
  require_known_license: true,
  user_agent: "StateWright-SAA-HOWTO/1"
}')

watch_result=$(saa_run internet-watch "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg url "http://127.0.0.1:$SAA_PORT/identity" \
  --argjson source_policy "$source_policy" '{
    workspace: $workspace,
    action: "register",
    source_policy: $source_policy,
    canonical_url: $url,
    source_group: "fixture.local",
    polling_interval_seconds: 3600,
    deterministic_jitter_seconds: 0
  }')")

watch_id=$(jq -er '.result.watch_id' <<<"$watch_result")
source_policy_id=$(jq -er '.result.watch.source_policy_id' <<<"$watch_result")
printf 'watch=%s\npolicy=%s\n' "$watch_id" "$source_policy_id"
```

`internet-watch register` writes an immutable source-policy record and an
immutable watch record. Later `enable`, `disable`, or `supersede` operations
create a new watch generation; they do not mutate the old record.

### 5.3 Schedule and Lease a Fetch Job

The scheduler creates jobs for active watches. The timestamps below are frozen
fixture evidence from September 3, 2026, not current wall-clock instructions.

```bash
schedule_result=$(saa_run internet-poll "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" '{
    workspace: $workspace,
    action: "schedule",
    scheduled_interval: "2026-09-03T00:00:00Z",
    earliest_start: "2026-09-03T00:00:00Z",
    deadline: "2026-09-03T00:05:00Z",
    retry_ceiling: 1
  }')")
job_id=$(jq -er '.result.job_ids[0]' <<<"$schedule_result")

lease_result=$(saa_run internet-poll "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg job_id "$job_id" '{
    workspace: $workspace,
    action: "lease",
    job_id: $job_id,
    worker_id: "howto-worker",
    acquired_at: "2026-09-03T00:00:01Z",
    expires_at: "2026-09-03T00:01:01Z"
  }')")
lease_id=$(jq -er '.result.lease_id' <<<"$lease_result")
```

For an operational scheduler, use `internet-poll select` with a current
canonical UTC timestamp to obtain due jobs before acquiring a lease. Lease
completion remains bound to the exact job and active lease generation.

### 5.4 Capture an Immutable Snapshot

```bash
capture_result=$(saa_run internet-fetch "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg job_id "$job_id" \
  --arg lease_id "$lease_id" '{
    workspace: $workspace,
    action: "execute",
    job_id: $job_id,
    lease_id: $lease_id,
    current_timestamp: "2026-09-03T00:00:02Z"
  }')")

snapshot_id=$(jq -er '.result.snapshot_id' <<<"$capture_result")
fetch_receipt_id=$(jq -er '.result.fetch_receipt_id' <<<"$capture_result")
artifact_bytes_id=$(jq -er '.result.artifact_bytes_id' <<<"$capture_result")
expected_sha256=$(jq -er '.body_sha256' "$STATEWRIGHT_FIXTURE_METADATA")
test "$artifact_bytes_id" = "artifact-bytes:sha256:$expected_sha256"
```

The fetch creates a receipt even when acquisition fails after a job and lease
exist. Successful response bytes are captured as a content-addressed artifact
before extraction or reasoning. The native provider also fetches `robots.txt`
for every origin in the redirect chain, fails closed unless permission is
established, and records the decision, document hash, status, and redirect
evidence in the fetch receipt.

### 5.5 Assess Source Admissibility

Robots permission and license classification are explicit assessment inputs;
StateWright does not infer permission from model prose. For a policy that does
not require a known license, a successful robots-backed fetch automatically
register host-generated assessment input with license classification `UNKNOWN`,
allowing supervisor runs to continue through assessment and extraction. Policies
that require a known license retain the manual provenance-bound path below.

```bash
assessment_result=$(saa_run internet-source "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg snapshot_id "$snapshot_id" \
  --arg fetch_receipt_id "$fetch_receipt_id" \
  --arg source_policy_id "$source_policy_id" '{
    workspace: $workspace,
    action: "assess",
    snapshot_id: $snapshot_id,
    fetch_receipt_id: $fetch_receipt_id,
    source_policy_id: $source_policy_id,
    robots_allowed: true,
    license_classification: "CC0-1.0"
  }')")

policy_assessment_id=$(jq -er '.result.assessment_id' <<<"$assessment_result")
jq -e '.result.assessment.status == "SOURCE_ADMISSIBLE"' \
  <<<"$assessment_result" >/dev/null
```

### 5.6 Extract Snapshot-Bound Fragments

```bash
extraction_result=$(saa_run internet-extract "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg snapshot_id "$snapshot_id" '{
    workspace: $workspace,
    action: "execute",
    snapshot_id: $snapshot_id
  }')")

extraction_receipt_id=$(jq -er \
  '.result.extraction_receipt_id' <<<"$extraction_result")
```

Optional request limits are `maximum_input_bytes`, `maximum_fragments`,
`maximum_fragment_bytes`, and `maximum_nesting_depth`. Lower them for an
individual extraction; do not raise them to bypass a source-policy rejection.

### 5.7 Feed Existing Knowledge First

```bash
feed_result=$(saa_run internet-improvement "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg policy_assessment_id "$policy_assessment_id" \
  --arg extraction_receipt_id "$extraction_receipt_id" '{
    workspace: $workspace,
    action: "feed",
    policy_assessment_id: $policy_assessment_id,
    extraction_receipt_id: $extraction_receipt_id,
    source_label: "local identity fixture",
    strict: false
  }')")
jq -e '.result.candidates[0].status == "VALIDATION_READY"' \
  <<<"$feed_result" >/dev/null

candidate_list=$(saa_run internet-candidate "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" '{
    workspace: $workspace,
    action: "list"
  }')")
candidate_id=$(jq -er \
  '.result.candidates[] |
   select(.payload.status == "VALIDATION_READY") |
   .object_id' <<<"$candidate_list")
```

The feed path uses `BrainFeedProcessor` and existing-knowledge-first retrieval.
Exact or equivalent known algorithms are excluded instead of creating a second
canonical owner.

Inspect the candidate and its retrieval evidence:

```bash
saa_run internet-candidate "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg candidate_id "$candidate_id" '{
    workspace: $workspace,
    action: "explain",
    candidate_id: $candidate_id
  }')" | jq '.result.retrieval_explanation'
```

### 5.8 Run Advisory OIEC-SR Reasoning

```bash
reasoning_result=$(saa_run internet-improvement "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg candidate_id "$candidate_id" '{
    workspace: $workspace,
    action: "reason",
    candidate_id: $candidate_id
  }')")

candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$reasoning_result")
jq -e '.result.analysis.authoritative == false' \
  <<<"$reasoning_result" >/dev/null
```

The qualified fixture does not require a provider. Deterministic fallback still
produces a source-bound advisory interpretation. Provider output cannot change
authoritative lifecycle states.

### 5.9 Qualify an Exact Internal Experiment

Create a request file so the exact benchmark and integrity inputs remain
readable:

```bash
cat >"$SAA_WORKSPACE/experiment.json" <<EOF
{
  "workspace": "$SAA_WORKSPACE",
  "action": "experiment-qualify",
  "candidate_id": "$candidate_id",
  "baseline_ref": "canonical-algorithm:sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "baseline_saa_ir": {
    "entry_nodes": ["constant"],
    "inputs": [{"name": "x", "position": 0}],
    "name": "constant-baseline",
    "nodes": [
      {
        "id": "constant",
        "operands": [{"constant": 0}],
        "primitive": "CONST"
      }
    ],
    "outputs": [
      {"name": "y", "position": 0, "source": {"node": "constant"}}
    ]
  },
  "dataset_snapshot_ids": ["$snapshot_id"],
  "trial_groups": [
    {
      "independence_group": "fixture-a",
      "deterministic_seed": 11,
      "inputs": [3],
      "expected_outputs": [3]
    },
    {
      "independence_group": "fixture-b",
      "deterministic_seed": 29,
      "inputs": [4],
      "expected_outputs": [4]
    }
  ],
  "minimum_material_effect": 1,
  "minimum_output": -10,
  "maximum_output": 10,
  "benchmark_track_scores": {
    "TRUTHGROUND": 10000,
    "MEANINGPATH": 10000,
    "SEMANTICREP": 10000,
    "MEANINGGROUND": 10000,
    "WORKGROUND": 10000,
    "PROGRESSCERT": 10000,
    "AGENTWORK": 10000
  },
  "integrity_snapshots": [
    {
      "generation": 1,
      "canonical_knowledge_count": 10,
      "corrected_error_opportunities": 10,
      "retrieval_queries": 10,
      "retrieval_correct_selections": 10,
      "equivalent_failure_opportunities": 10
    },
    {
      "generation": 2,
      "canonical_knowledge_count": 11,
      "corrected_error_opportunities": 11,
      "retrieval_queries": 11,
      "retrieval_correct_selections": 11,
      "equivalent_failure_opportunities": 11
    }
  ],
  "recorded_at": "2026-09-03T01:00:00Z"
}
EOF

experiment_result=$("$STATEWRIGHT" internet-improvement \
  "@$SAA_WORKSPACE/experiment.json")
jq -e '.ok == true and
       .result.qualification.status == "EXPERIMENT_QUALIFIED"' \
  <<<"$experiment_result" >/dev/null
candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$experiment_result")
evidence_ids=$(jq -c '.result.qualification.evidence_ids' \
  <<<"$experiment_result")
```

Only supported internal SAA IR executes. The first release qualifies exact
scalar one-input/one-output `IDENTITY` and `CONST` programs. Internet source code
is not compiled or invoked.

### 5.10 Assess the Autonomous Promotion Policy

```bash
policy_result=$(saa_run internet-promotion-policy "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" '{
    workspace: $workspace,
    action: "register-default"
  }')")
promotion_policy_id=$(jq -er '.result.policy_id' <<<"$policy_result")

promotion_result=$(saa_run internet-improvement "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg candidate_id "$candidate_id" \
  --arg policy_id "$promotion_policy_id" '{
    workspace: $workspace,
    action: "policy-assess",
    current_timestamp: "2026-09-03T01:00:01Z",
    candidate_id: $candidate_id,
    policy_id: $policy_id
  }')")

candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$promotion_result")
jq -e '.result.assessment.promotion_allowed == true and
       .result.assessment.human_approval_required == false' \
  <<<"$promotion_result" >/dev/null
```

The policy is conjunctive. A failed hard predicate, missing policy, stale source,
bad signature, unsupported capability, successful falsifier, integrity failure,
or insufficient evidence blocks promotion.

### 5.11 Admit the Candidate to Probation

The fixture supplies a synthetic previous preference to prove restoration after
demotion:

```bash
previous_ref="canonical-algorithm:sha256:$(printf '%064d' 7)"
admission_result=$(saa_run internet-improvement "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg candidate_id "$candidate_id" \
  --arg previous_ref "$previous_ref" '{
    workspace: $workspace,
    action: "probation-admit",
    current_timestamp: "2026-09-03T01:00:02Z",
    candidate_id: $candidate_id,
    previous_preferred_canonical_ref: $previous_ref
  }')")

candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$admission_result")
jq -e '.result.updated_candidate.status == "PROBATIONARY_CANONICAL"' \
  <<<"$admission_result" >/dev/null
```

Probationary admission is not full canonical preference. Admission supports
exact identity and the bounded nonconstant affine family described in
`SAA_INTERNET_NEXT_STEPS_IMPLEMENTATION.md`. Policy assessment and admission
require an explicit `current_timestamp` in canonical UTC; the fixture times
above are for deterministic tests only. Source age is rechecked at admission
and observation, including fresh assessment evidence from conditional fetches.

### 5.12 Select Four Deterministic Canary Uses

The packaged policy requires four observations across two windows. Canary
selection is deterministic from the policy, candidate, and query signature.

```bash
selected_queries=()
for index in $(seq 0 1000); do
  query_signature=$(printf 'howto-query-%s' "$index" | sha256sum | awk '{print $1}')
  selection_result=$(saa_run internet-probation "$(jq -cn \
    --arg workspace "$SAA_WORKSPACE" \
    --arg candidate_id "$candidate_id" \
    --arg query_signature "$query_signature" '{
      workspace: $workspace,
      action: "select",
      candidate_id: $candidate_id,
      query_signature: $query_signature
    }')")
  if jq -e '.result.candidate_selected == true' \
    <<<"$selection_result" >/dev/null; then
    selected_queries+=("$query_signature")
  fi
  [[ ${#selected_queries[@]} -eq 4 ]] && break
done
test ${#selected_queries[@]} -eq 4
```

### 5.13 Register Successful Observations

```bash
for index in 0 1 2 3; do
  context_signature=$(printf 'howto-context-%s' "$index" | sha256sum | awk '{print $1}')
  observation_result=$(saa_run internet-improvement "$(jq -cn \
    --arg workspace "$SAA_WORKSPACE" \
    --arg candidate_id "$candidate_id" \
    --arg query_signature "${selected_queries[$index]}" \
    --arg context_signature "$context_signature" \
    --arg observed_at "2026-09-03T02:00:0${index}Z" \
    --argjson evidence_ids "$evidence_ids" \
    --argjson window_index "$((index % 2))" '{
      workspace: $workspace,
      action: "probation-observe",
      candidate_id: $candidate_id,
      query_signature: $query_signature,
      context_signature: $context_signature,
      observed_at: $observed_at,
      window_index: $window_index,
      candidate_correct: true,
      baseline_correct: false,
      invariant_passed: true,
      benchmark_passed: true,
      integrity_passed: true,
      source_valid: true,
      reproduction_passed: true,
      evidence_ids: $evidence_ids
    }')")
  candidate_id=$(jq -er '.result.updated_candidate_id' \
    <<<"$observation_result")
done

jq -e '.result.updated_candidate.status == "CANONICAL" and
       .result.promotion_decision.human_approval_required == false' \
  <<<"$observation_result" >/dev/null
```

Promotion occurs only after the complete successful window set. No person,
signature, confirmation, or approval object is requested.

### 5.14 Inject a Regression and Observe Demotion

```bash
demotion_context=$(printf 'howto-demotion-context' | sha256sum | awk '{print $1}')
demotion_result=$(saa_run internet-improvement "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg candidate_id "$candidate_id" \
  --arg query_signature "${selected_queries[0]}" \
  --arg context_signature "$demotion_context" \
  --argjson evidence_ids "$evidence_ids" '{
    workspace: $workspace,
    action: "probation-observe",
    candidate_id: $candidate_id,
    query_signature: $query_signature,
    context_signature: $context_signature,
    observed_at: "2026-09-03T02:01:00Z",
    window_index: 1,
    candidate_correct: false,
    baseline_correct: true,
    invariant_passed: true,
    benchmark_passed: false,
    integrity_passed: true,
    source_valid: true,
    reproduction_passed: true,
    evidence_ids: $evidence_ids
  }')")

candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$demotion_result")
jq -e '.result.updated_candidate.status == "DEMOTED" and
       .result.demotion_decision.human_approval_required == false' \
  <<<"$demotion_result" >/dev/null
```

Verify that retrieval restores the previous preference:

```bash
restoration_result=$(saa_run internet-probation "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  --arg candidate_id "$candidate_id" \
  --arg query_signature "${selected_queries[0]}" '{
    workspace: $workspace,
    action: "select",
    candidate_id: $candidate_id,
    query_signature: $query_signature
  }')")

jq -e --arg previous_ref "$previous_ref" \
  '.result.candidate_selected == false and
   .result.selected_canonical_ref == $previous_ref' \
  <<<"$restoration_result" >/dev/null
```

### 5.15 Rebuild and Verify Derived State

```bash
saa_run internet-integrity "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" '{
    workspace: $workspace,
    action: "rebuild"
  }')" | jq '.result'

saa_run internet-integrity "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" '{
    workspace: $workspace,
    action: "verify"
  }')" | jq '.result'
```

`rebuild` recreates derived internet, canonical-algorithm, and governance
projections from immutable records, then verifies integrity. It does not rewrite
the object store or event history.

### 5.16 Verify the Approval-Free SAA Path

```bash
approval_records=$(saa_run retrieve "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" '{
    workspace: $workspace,
    query: "approval",
    object_type: "approval",
    limit: 10
  }')")
jq -e '.result.objects | length == 0' <<<"$approval_records" >/dev/null

if "$STATEWRIGHT" internet-improvement "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" '{
    workspace: $workspace,
    action: "approve"
  }')" >/dev/null 2>&1; then
  printf 'unexpected internet approval action\n' >&2
  exit 1
fi
```

The rejected action is an invariant check, not a step to add back. General EGCF
execution approvals remain a separate command-fabric concern.

## 6. Inspection and Control Recipes

All examples in this section use `SAA_WORKSPACE` and IDs produced by the manual
walkthrough.

### 6.1 Watches

```bash
saa_run internet-watch "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "list"}')" | jq '.result'

saa_run internet-watch "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" --arg watch_id "$watch_id" \
  '{workspace: $workspace, action: "get", watch_id: $watch_id}')" | jq '.result'
```

`enable`, `disable`, and `supersede` accept `watch_id` and create an immutable
successor watch. Optional replacement fields include `source_policy`,
`source_policy_id`, `canonical_url`, `source_group`, `accepted_mime_types`,
polling and jitter values, fetch limits, timeout, and `schedule_generation`.

### 6.2 Jobs, Leases, and Receipts

```bash
saa_run internet-poll "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "list"}')" | jq '.result'

saa_run internet-poll "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" '{
    workspace: $workspace,
    action: "select",
    current_timestamp: "2026-09-03T00:00:02Z",
    global_concurrency: 4,
    per_source_group_concurrency: 1
  }')" | jq '.result'

saa_run internet-fetch "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "list"}')" | jq '.result.receipts'
```

`internet-poll select` is read-only. It applies current leases, concurrency,
response-byte, CPU-unit, and clock-jump limits to deterministic job ordering.

### 6.3 Source Records and Extractions

List one source record kind at a time:

```bash
for kind in policies receipts snapshots assessments extractions fragments; do
  saa_run internet-source "$(jq -cn \
    --arg workspace "$SAA_WORKSPACE" --arg kind "$kind" \
    '{workspace: $workspace, action: "list", kind: $kind}')" \
    | jq --arg kind "$kind" '.result[$kind]'
done

saa_run internet-source "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" --arg object_id "$snapshot_id" \
  '{workspace: $workspace, action: "get", object_id: $object_id}')" \
  | jq '.result'

saa_run internet-extract "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "list"}')" | jq '.result.extractions'
```

`internet-source get` retrieves the exact supplied EGCF object ID. Confirm the
returned `object_type`; the command does not independently narrow the ID to a
source-only type.

### 6.4 Candidates and Deterministic Advancement

```bash
saa_run internet-candidate "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" --arg candidate_id "$candidate_id" \
  '{workspace: $workspace, action: "get", candidate_id: $candidate_id}')" \
  | jq '.result'

saa_run internet-improvement "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "status"}')" | jq '.result'
```

`internet-improvement advance` is a compatibility adapter for a
candidate-scoped `run-once`. The core Director, not the CLI, selects the next
transition. It is not a gate bypass: qualification requires an active registered
experiment protocol, promotion requires the selected immutable policy, and
probation observation requires a registered provenance-bound observation input.

### 6.5 Director and Bounded Orchestrator

Create a read-only signed plan:

```bash
orchestration_request=$(jq -cn --arg workspace "$SAA_WORKSPACE" '{
  workspace: $workspace,
  current_timestamp: "2026-09-04T00:00:00Z",
  cycle_key: "2026-09-04T00:00:00Z",
  worker_id: "saa-timer-worker",
  action_lease_expires_at: "2026-09-04T00:01:00Z",
  fetch_lease_expires_at: "2026-09-04T00:01:00Z",
  policy: {
    maximum_actions: 1,
    maximum_provider_calls: 1,
    action_deadline: "2026-09-04T00:05:00Z"
  }
}')

saa_run internet-improvement \
  "$(jq -c '. + {action:"plan"}' <<<"$orchestration_request")" \
  | jq '.result.plan'
```

Execute at most one directed action and persist its plan, run, run events,
action lease, and terminal receipt:

```bash
run_result=$(saa_run internet-improvement \
  "$(jq -c '. + {action:"run-once"}' <<<"$orchestration_request")")
run_id=$(jq -er '.result.run_id' <<<"$run_result")
action_key=$(jq -er '.result.action_key // empty' <<<"$run_result")

saa_run internet-improvement "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" --arg run_id "$run_id" \
  '{workspace:$workspace,action:"run-status",run_id:$run_id}')" \
  | jq '.result'

if [[ -n $action_key ]]; then
  saa_run internet-improvement "$(jq -cn \
    --arg workspace "$SAA_WORKSPACE" --arg action_key "$action_key" \
    '{workspace:$workspace,action:"explain-action",action_key:$action_key}')" \
    | jq '.result'
fi
```

Resume a prior run by supplying its ID and a fresh bounded timestamp/lease
request:

```bash
saa_run internet-improvement \
  "$(jq -c --arg run_id "$run_id" '. + {action:"resume",run_id:$run_id}' \
    <<<"$orchestration_request")" | jq '.result'
```

The first release is intentionally process-bounded. Operate it from a system
timer or job runner that invokes `run-once`, waits for process exit, and then
invokes it again. The EGCF workspace lock is held for one invocation, so this
release does not claim a continuously running multi-process daemon or
exactly-once HTTP transport.

#### Supervisor Mode

The installed C++20 executable `statewright-internet-supervisor` implements the
bounded timer/job-runner layer without opening the EGCF store itself. Each cycle
queries worker-filtered nonterminal status, resumes one expired or unleased run
when necessary, or launches one `run-once` child. It enforces child timeouts,
wall time, cycle and failure ceilings, exponential backoff, output limits,
stable worker identity, process-group termination, and canonical JSONL events.

```bash
statewright-internet-supervisor \
  --workspace "$SAA_WORKSPACE" \
  --worker-id saa-internet-host01 \
  --maximum-cycles 8 \
  --maximum-wall-seconds 300 \
  --child-timeout-seconds 120 \
  --action-lease-seconds 180 \
  --fetch-lease-seconds 180 \
  --action-deadline-seconds 120 \
  --event-log "$SAA_WORKSPACE/supervisor/internet-events.jsonl"
```

Use one stable worker ID and one supervisor per workspace. An unexpired active
lease produces `RECOVERY_DEFERRED`; a later timer wake resumes after expiry.
The full request-template, event, exit-status, recovery, and systemd contracts
are in `docs/SAA_INTERNET_SUPERVISOR_MODE.md`.

### 6.6 Promotion Policies and Probation History

```bash
saa_run internet-promotion-policy "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "list"}')" | jq '.result'

saa_run internet-promotion-policy "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" --arg object_id "$promotion_policy_id" \
  '{workspace: $workspace, action: "get", object_id: $object_id}')" \
  | jq '.result'

saa_run internet-probation "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "list"}')" | jq '.result'
```

Promotion assessments persist every predicate, `source_age_seconds`, `assessed_at`,
and `source_checked_at`. Source age is measured at the supplied current timestamp
against the latest receipt-backed assessment of the same snapshot and policy.
A conditional HTTP 304 can refresh this evidence without changing the snapshot.
Missing, malformed, future, or stale timestamps fail closed; experiment time
does not freeze source age. Admission and probation observations recheck freshness.

### 6.7 Migrate Legacy Candidate Lineage

Only use migration for an actual pre-probation candidate that loads under its
original exact signature:

```bash
saa_run internet-candidate "$(jq -cn \
  --arg workspace "$SAA_WORKSPACE" --arg candidate_id "$legacy_candidate_id" '{
    workspace: $workspace,
    action: "migrate",
    candidate_id: $candidate_id
  }')" | jq '.result'
```

Migration creates an immutable current successor and supersedence record. It
does not rewrite the legacy payload.

## 7. Bounded Real-Internet Operation

Complete the local fixture first. Public acquisition is opt-in and must remain
policy-bound.

The packaged default source policy allows only HTTPS on port 443, verifies TLS,
requires robots permission, rejects loopback, limits redirects and response
sizes, and accepts JSON, HTML, and plain text. Inspect it before registration:

```bash
jq . resources/policies/internet/default-source-policy-v1.json
```

Create an operational policy from a reviewed copy rather than weakening the
fixture policy. The following URL is a placeholder and is not a live fixture:

```bash
export SOURCE_URL='https://your-approved-source.example/path'
export SOURCE_GROUP='your-approved-source.example'
export LIVE_WORKSPACE="$(mktemp -d /tmp/statewright-saa-live.XXXXXX)"

live_policy=$(cat resources/policies/internet/default-source-policy-v1.json)
live_watch=$(saa_run internet-watch "$(jq -cn \
  --arg workspace "$LIVE_WORKSPACE" \
  --arg url "$SOURCE_URL" \
  --arg source_group "$SOURCE_GROUP" \
  --argjson source_policy "$live_policy" '{
    workspace: $workspace,
    action: "register",
    source_policy: $source_policy,
    canonical_url: $url,
    source_group: $source_group,
    polling_interval_seconds: 86400,
    deterministic_jitter_seconds: 0
  }')")
```

Before scheduling, verify all of the following outside model output:

1. the exact URL and source group are intended;
2. the resolved destination is public and the port is permitted;
3. authentication, cookies, browser profiles, ambient credentials, and proxy
   authentication are not required;
4. the fetch receipt contains successful per-origin robots evidence;
5. license classification is known or the policy explicitly permits `UNKNOWN`;
6. MIME, size, decompression, redirect, TLS, and timeout limits are suitable;
7. the workspace is disposable for the first trial;
8. no linked-domain crawl or JavaScript execution is expected.

After capture, inspect the fetch receipt, snapshot hash, selected headers,
redirect chain, resolved addresses, TLS result, policy assessment, extraction,
and source age before feeding a candidate.

Never work around a policy rejection by enabling loopback, disabling TLS,
raising bounds without review, editing a persisted record, or executing source
content.

## 8. Troubleshooting by Lifecycle Boundary

### 8.1 Resource or Schema Verification Fails

Run:

```bash
Tools/verify_release_inputs.sh
```

Do not edit `resources/manifest.sha256` to silence a mismatch. Restore or
regenerate the intended source-controlled resource through its canonical update
process, then re-run the verifier.

### 8.2 Watch Registration Is Rejected

Check `allowed_schemes`, `allowed_ports`, MIME types, TLS requirements, loopback
policy, and required fields. Use `internet-watch list` to distinguish an absent
watch from a superseded or disabled generation.

### 8.3 No Job Is Selected

Inspect:

```bash
saa_run internet-watch "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "list"}')" | jq '.result.active_watch_ids'
saa_run internet-poll "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "list"}')" | jq '.result'
```

Check the watch is enabled, the schedule generation is current, the job is due,
concurrency budgets permit it, and no active lease already owns it.

### 8.4 Lease Completion Is Rejected

An expired worker or mismatched job/lease pair cannot finalize a fetch. Acquire
a valid successor lease through the scheduler. Do not edit timestamps or lease
records.

### 8.5 Fetch Fails Closed

Use `internet-fetch list` and `internet-poll list`. Common boundaries include a
private or changed DNS answer, prohibited redirect or port, TLS failure,
credentials/proxy rejection, malformed headers, timeout, response-size limit,
or decompression limit. A durable failure receipt may identify the exact stage.

### 8.6 Snapshot or Projection Integrity Fails

Do not modify content-addressed files. If immutable objects and artifacts are
intact but SQLite is stale or corrupt, run:

```bash
saa_run internet-integrity "$(jq -cn --arg workspace "$SAA_WORKSPACE" \
  '{workspace: $workspace, action: "rebuild"}')"
```

If immutable bytes or signatures are tampered, restore the complete workspace
from a known-good backup; projection rebuild cannot legitimize altered evidence.

### 8.7 Extraction Is Empty or Quarantined

Inspect the snapshot content type, exact bytes, policy assessment, extraction
receipt, fragment limits, encoding, and nesting depth. Unsupported or ambiguous
content must remain quarantined. Do not ask a model to override extraction or
source policy.

### 8.8 No Novel Candidate Is Created

Run `internet-candidate explain` for any staged candidate and inspect the
retrieval receipt. Exact, equivalent, related, transfer, adaptation, and known
failure matches can intentionally exclude a new canonical candidate.

### 8.9 Reasoning Provider Is Unavailable

The deterministic fallback is the supported degraded mode. Confirm the result
is source-bound and `.analysis.authoritative == false`. Provider availability is
not a promotion gate by itself.

### 8.10 Experiment Qualification Fails

Check that:

- candidate and baseline use supported exact scalar SAA IR;
- both run against the same frozen context and dataset snapshots;
- trial groups have independent identities and deterministic seeds;
- expected outputs and exact rational values are valid;
- all benchmark tracks are present and correctly shaped;
- hard output bounds and invariants pass;
- integrity trajectory is valid;
- known equivalent failures do not recur.

Downloaded source code is never an acceptable substitute for internal SAA IR.

### 8.11 Promotion Is Blocked

Inspect `internet-promotion-policy list` and the exact assessment predicates.
Typical blockers include missing/mismatched policy, stale source age,
insufficient independent source or experiment groups, unresolved contradiction,
successful falsifier, prohibited capability, reproduction failure, benchmark or
integrity failure, and unsupported mathematical or semantic strength.

Popularity, source count, model confidence, and an approval record cannot satisfy
a failed predicate.

### 8.12 Probation Does Not Promote

Inspect `internet-probation list`. Confirm the candidate was selected for each
canary query, all observations carry evidence IDs, the required number of uses
and observations exists, both configured windows are covered, and every hard
observation field passes.

### 8.13 Candidate Is Automatically Demoted

Demotion is expected when an automatic demotion predicate fires. Inspect the
demotion decision and observation, confirm the candidate is excluded from
preferred selection, and verify the previous canonical preference was restored.
Do not delete the demotion record or re-label the old candidate.

### 8.14 Legacy Candidate Cannot Load

Migration accepts only an exact valid legacy signature and supported
pre-probation lineage. Use `internet-candidate migrate`; do not add missing
fields directly to the old JSON object.

## 9. Backup and Disposable Cleanup

For backup or forensic replay, copy the complete workspace state while no writer
is active:

```bash
cp -a "$SAA_WORKSPACE/.ourd-agent" /path/to/backup/location/
```

Do not back up only `projection.sqlite3`; it is derived. Do not selectively
delete immutable objects, artifacts, events, admissions, promotion decisions,
or demotion decisions.

For tutorials, remove the entire disposable workspace after stopping the fixture
server. The cleanup trap in this HOWTO does that automatically. For operational
state, follow an explicit retention and backup procedure instead of using the
tutorial cleanup command.

## 10. Qualification and Evidence

Run the complete preset matrix:

```bash
cmake --preset developer
cmake --build --preset developer -j2
ctest --preset developer --output-on-failure

cmake --preset sanitizer
cmake --build --preset sanitizer -j2
ctest --preset sanitizer --output-on-failure

cmake --preset release
cmake --build --preset release -j2
ctest --preset release --output-on-failure
```

Run the HOWTO-specific validation wrapper:

```bash
Tests/saa_internet_howto_smoke.sh \
  docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md \
  build/release/statewright \
  build/release/Tests/statewright_internet_fixture_server \
  resources/fixtures/internet/identity-v1.json
```

Run the Director/Orchestrator persistence and fault-receipt smoke tests:

```bash
Tests/internet_orchestrator_cli_smoke.sh build/release/statewright
Tests/internet_orchestrator_fault_smoke.sh build/release/statewright
```

Generate a new non-overwriting evidence bundle. Use a new output path for every
source state:

```bash
STATEWRIGHT_QUALIFICATION_DATE=2026-09-04 \
  Tools/generate_internet_release_evidence.sh \
  build/release-evidence/internet-howto-release-2026-09-04
```

The resulting `internet-qualification-status.json` must retain:

- `qualification_status: QUALIFIED_FIXTURE_SCOPE`;
- `human_approval_required: false`;
- `approval_operation_present: false`;
- `approval_records_created: 0`;
- autonomous promotion, demotion, and restoration verification;
- no EGCF authority expansion;
- no production cutover or release approval claim;
- no live internet, Python runtime, or downloaded-code execution;
- the Linux/POSIX, `IDENTITY`/`CONST`, `IDENTITY`-admission, single-host limits.

Verify the bundle from its directory:

```bash
(
  cd build/release-evidence/internet-howto-release-2026-09-04
  sha256sum -c manifest.sha256
)
```

## 11. Qualified Limits

The first release does not claim:

- authenticated browsing, cookies, browser profiles, or ambient credentials;
- JavaScript execution, a general browser engine, or CAPTCHA solving;
- arbitrary crawling or search-engine indexing;
- execution or compilation of downloaded source or binaries;
- model-weight training or fine-tuning;
- distributed multi-host scheduling;
- macOS or Windows qualification;
- SAA experiment coverage beyond exact scalar one-input/one-output `IDENTITY`
  and `CONST`;
- probationary canonical admission beyond exact `IDENTITY` candidates;
- automatic EGCF C3/C5 command authority;
- production ownership cutover or release approval.

See `docs/RESIDUAL_RISKS.md` for the complete current limitation inventory.

## 12. Maintenance Traceability

When the subsystem changes, update this HOWTO only after checking the canonical
owner for each fact:

| HOWTO fact | Canonical owner |
| --- | --- |
| operation and request field | `Apps/statewright/cli.cpp` |
| complete fixture transition and assertion | `Tests/internet_cli_smoke.sh` |
| source policy defaults | `resources/policies/internet/default-source-policy-v1.json` |
| promotion and probation defaults | `resources/policies/internet/default-promotion-policy-v1.json` |
| immutable record and projection behavior | EGCF store and internet-store sources |
| security and fetch limits | source policy, fetch, and libcurl provider sources |
| qualification boundary | `docs/SAA_INTERNET_IMPLEMENTATION_AUDIT.md` |
| unresolved limitation | `docs/RESIDUAL_RISKS.md` |

Re-run `Tests/saa_internet_howto_smoke.sh` after changing command names,
request fields, lifecycle states, fixture metadata, approval semantics,
qualified limits, or referenced paths. The validation wrapper intentionally
reuses the canonical internet fixture smoke rather than implementing a second
promotion engine.
