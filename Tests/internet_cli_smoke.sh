#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  printf 'usage: %s STATEWRIGHT FIXTURE_SERVER FIXTURE_METADATA\n' "$0" >&2
  exit 2
fi

statewright=$1
fixture_server=$2
fixture_metadata=$3
root=$(mktemp -d /tmp/statewright-internet-cli-smoke.XXXXXX)
port_file="$root/port"
server_pid=

cleanup() {
  if [[ -n ${server_pid:-} ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$root"
}
trap cleanup EXIT

"$fixture_server" "$port_file" &
server_pid=$!
for _ in $(seq 1 100); do
  if [[ -s $port_file ]]; then
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    printf 'internet fixture server exited before publishing its port\n' >&2
    exit 1
  fi
  sleep 0.02
done
if [[ ! -s $port_file ]]; then
  printf 'internet fixture server did not publish its port\n' >&2
  exit 1
fi
port=$(tr -d '[:space:]' <"$port_file")

run() {
  local operation=$1
  local request=$2
  local output
  if ! output=$("$statewright" "$operation" "$request"); then
    printf '%s failed: %s\n' "$operation" "$output" >&2
    return 1
  fi
  jq -e '.ok == true' <<<"$output" >/dev/null
  printf '%s\n' "$output"
}

policy=$(jq -cn --argjson port "$port" '{
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
  user_agent: "StateWright-SAA-Fixture/1"
}')
watch_request=$(jq -cn \
  --arg workspace "$root" \
  --arg url "http://127.0.0.1:$port/identity" \
  --argjson policy "$policy" '{
    workspace: $workspace,
    action: "register",
    source_policy: $policy,
    canonical_url: $url,
    source_group: "fixture.local",
    polling_interval_seconds: 3600,
    deterministic_jitter_seconds: 0
  }')
watch=$(run internet-watch "$watch_request")
watch_id=$(jq -er '.result.watch_id' <<<"$watch")
source_policy_id=$(jq -er '.result.watch.source_policy_id' <<<"$watch")

schedule=$(run internet-poll "$(jq -cn \
  --arg workspace "$root" '{
    workspace: $workspace,
    action: "schedule",
    scheduled_interval: "2026-09-03T00:00:00Z",
    earliest_start: "2026-09-03T00:00:00Z",
    deadline: "2026-09-03T00:05:00Z",
    retry_ceiling: 1
  }')")
job_id=$(jq -er '.result.job_ids[0]' <<<"$schedule")
jq -e --arg watch_id "$watch_id" \
  '.result.jobs[0].watch_id == $watch_id' <<<"$schedule" >/dev/null

lease=$(run internet-poll "$(jq -cn \
  --arg workspace "$root" --arg job_id "$job_id" '{
    workspace: $workspace,
    action: "lease",
    job_id: $job_id,
    worker_id: "fixture-worker",
    acquired_at: "2026-09-03T00:00:01Z",
    expires_at: "2026-09-03T00:01:01Z"
  }')")
lease_id=$(jq -er '.result.lease_id' <<<"$lease")

capture=$(run internet-fetch "$(jq -cn \
  --arg workspace "$root" --arg job_id "$job_id" --arg lease_id "$lease_id" '{
    workspace: $workspace,
    action: "execute",
    job_id: $job_id,
    lease_id: $lease_id,
    current_timestamp: "2026-09-03T00:00:02Z"
  }')")
snapshot_id=$(jq -er '.result.snapshot_id' <<<"$capture")
fetch_receipt_id=$(jq -er '.result.fetch_receipt_id' <<<"$capture")
expected_body_sha256=$(jq -er '.body_sha256' "$fixture_metadata")
jq -e --arg expected "$expected_body_sha256" \
  '.result.artifact_bytes_id == ("artifact-bytes:sha256:" + $expected)' \
  <<<"$capture" >/dev/null

assessment=$(run internet-source "$(jq -cn \
  --arg workspace "$root" \
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
policy_assessment_id=$(jq -er '.result.assessment_id' <<<"$assessment")
jq -e '.result.assessment.status == "SOURCE_ADMISSIBLE"' \
  <<<"$assessment" >/dev/null

extraction=$(run internet-extract "$(jq -cn \
  --arg workspace "$root" --arg snapshot_id "$snapshot_id" '{
    workspace: $workspace,
    action: "execute",
    snapshot_id: $snapshot_id
  }')")
extraction_receipt_id=$(jq -er '.result.extraction_receipt_id' <<<"$extraction")

feed=$(run internet-improvement "$(jq -cn \
  --arg workspace "$root" \
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
  <<<"$feed" >/dev/null
candidate_list=$(run internet-candidate "$(jq -cn --arg workspace "$root" \
  '{workspace: $workspace, action: "list"}')")
candidate_id=$(jq -er \
  '.result.candidates[] | select(.payload.status == "VALIDATION_READY") | .object_id' \
  <<<"$candidate_list")

reasoning=$(run internet-improvement "$(jq -cn \
  --arg workspace "$root" --arg candidate_id "$candidate_id" '{
    workspace: $workspace,
    action: "reason",
    candidate_id: $candidate_id
  }')")
candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$reasoning")
jq -e '.result.analysis.authoritative == false' <<<"$reasoning" >/dev/null

experiment_request=$(jq -cn \
  --arg workspace "$root" \
  --arg candidate_id "$candidate_id" \
  --arg snapshot_id "$snapshot_id" '{
    workspace: $workspace,
    action: "experiment-qualify",
    candidate_id: $candidate_id,
    baseline_ref: ("canonical-algorithm:sha256:" + ("a" * 64)),
    baseline_saa_ir: {
      entry_nodes: ["constant"],
      inputs: [{name: "x", position: 0}],
      name: "constant-baseline",
      nodes: [{id: "constant", operands: [{constant: 0}], primitive: "CONST"}],
      outputs: [{name: "y", position: 0, source: {node: "constant"}}]
    },
    dataset_snapshot_ids: [$snapshot_id],
    trial_groups: [
      {independence_group: "fixture-a", deterministic_seed: 11,
       inputs: [3], expected_outputs: [3]},
      {independence_group: "fixture-b", deterministic_seed: 29,
       inputs: [4], expected_outputs: [4]}
    ],
    minimum_material_effect: 1,
    minimum_output: -10,
    maximum_output: 10,
    benchmark_track_scores: {
      TRUTHGROUND: 10000,
      MEANINGPATH: 10000,
      SEMANTICREP: 10000,
      MEANINGGROUND: 10000,
      WORKGROUND: 10000,
      PROGRESSCERT: 10000,
      AGENTWORK: 10000
    },
    integrity_snapshots: [
      {generation: 1, canonical_knowledge_count: 10,
       corrected_error_opportunities: 10, retrieval_queries: 10,
       retrieval_correct_selections: 10,
       equivalent_failure_opportunities: 10},
      {generation: 2, canonical_knowledge_count: 11,
       corrected_error_opportunities: 11, retrieval_queries: 11,
       retrieval_correct_selections: 11,
       equivalent_failure_opportunities: 11}
    ],
    recorded_at: "2026-09-03T01:00:00Z"
  }')
experiment=$(run internet-improvement "$experiment_request")
candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$experiment")
evidence_ids=$(jq -c '.result.qualification.evidence_ids' <<<"$experiment")
jq -e '.result.qualification.status == "EXPERIMENT_QUALIFIED"' \
  <<<"$experiment" >/dev/null

promotion_policy=$(run internet-promotion-policy "$(jq -cn \
  --arg workspace "$root" '{workspace: $workspace, action: "register-default"}')")
promotion_policy_id=$(jq -er '.result.policy_id' <<<"$promotion_policy")
promotion=$(run internet-improvement "$(jq -cn \
  --arg workspace "$root" \
  --arg candidate_id "$candidate_id" \
  --arg policy_id "$promotion_policy_id" '{
    workspace: $workspace,
    action: "policy-assess",
    current_timestamp: "2026-09-03T01:00:01Z",
    candidate_id: $candidate_id,
    policy_id: $policy_id
  }')")
candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$promotion")
jq -e '.result.assessment.promotion_allowed == true and
       .result.assessment.human_approval_required == false' \
  <<<"$promotion" >/dev/null

previous_ref="canonical-algorithm:sha256:$(printf '%064d' 7)"
admission=$(run internet-improvement "$(jq -cn \
  --arg workspace "$root" \
  --arg candidate_id "$candidate_id" \
  --arg previous_ref "$previous_ref" '{
    workspace: $workspace,
    action: "probation-admit",
    current_timestamp: "2026-09-03T01:00:02Z",
    candidate_id: $candidate_id,
    previous_preferred_canonical_ref: $previous_ref
  }')")
candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$admission")
jq -e '.result.updated_candidate.status == "PROBATIONARY_CANONICAL"' \
  <<<"$admission" >/dev/null

selected_queries=()
for index in $(seq 0 1000); do
  query_signature=$(printf 'fixture-query-%s' "$index" | sha256sum | awk '{print $1}')
  selection=$(run internet-probation "$(jq -cn \
    --arg workspace "$root" \
    --arg candidate_id "$candidate_id" \
    --arg query_signature "$query_signature" '{
      workspace: $workspace,
      action: "select",
      candidate_id: $candidate_id,
      query_signature: $query_signature
    }')")
  if jq -e '.result.candidate_selected == true' <<<"$selection" >/dev/null; then
    selected_queries+=("$query_signature")
  fi
  if [[ ${#selected_queries[@]} -eq 4 ]]; then
    break
  fi
done
if [[ ${#selected_queries[@]} -ne 4 ]]; then
  printf 'failed to find four deterministic canary queries\n' >&2
  exit 1
fi

for index in 0 1 2 3; do
  context_signature=$(printf 'fixture-context-%s' "$index" | sha256sum | awk '{print $1}')
  observation=$(run internet-improvement "$(jq -cn \
    --arg workspace "$root" \
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
  candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$observation")
done
jq -e '.result.updated_candidate.status == "CANONICAL" and
       .result.promotion_decision.human_approval_required == false' \
  <<<"$observation" >/dev/null

demotion_context=$(printf 'fixture-demotion-context' | sha256sum | awk '{print $1}')
demotion=$(run internet-improvement "$(jq -cn \
  --arg workspace "$root" \
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
candidate_id=$(jq -er '.result.updated_candidate_id' <<<"$demotion")
jq -e '.result.updated_candidate.status == "DEMOTED" and
       .result.demotion_decision.human_approval_required == false' \
  <<<"$demotion" >/dev/null

restored=$(run internet-probation "$(jq -cn \
  --arg workspace "$root" \
  --arg candidate_id "$candidate_id" \
  --arg query_signature "${selected_queries[0]}" '{
    workspace: $workspace,
    action: "select",
    candidate_id: $candidate_id,
    query_signature: $query_signature
  }')")
jq -e --arg previous_ref "$previous_ref" \
  '.result.candidate_selected == false and
   .result.selected_canonical_ref == $previous_ref' <<<"$restored" >/dev/null

run internet-integrity "$(jq -cn --arg workspace "$root" \
  '{workspace: $workspace, action: "rebuild"}')" >/dev/null
run internet-integrity "$(jq -cn --arg workspace "$root" \
  '{workspace: $workspace, action: "verify"}')" >/dev/null

approval_search=$(run retrieve "$(jq -cn --arg workspace "$root" \
  '{workspace: $workspace, query: "approval", object_type: "approval", limit: 10}')")
jq -e '.result.objects | length == 0' <<<"$approval_search" >/dev/null

if "$statewright" internet-improvement "$(jq -cn --arg workspace "$root" \
  '{workspace: $workspace, action: "approve"}')" >/dev/null 2>&1; then
  printf 'internet-improvement unexpectedly accepted an approval action\n' >&2
  exit 1
fi

# Polynomial command input validation must fail before storing a design or
# collecting timings. These are rejection tests, not qualification evidence.
measurement_request=$(jq -cn --arg workspace "$root" '{
  workspace: $workspace,
  action: "polynomial-measurement-freeze",
  candidate_id: "not-a-candidate",
  protocol_freeze_id: "not-a-freeze",
  inputs: ["0"],
  repetitions: 2
}')
readiness_request=$(jq -cn --arg workspace "$root" \
  '{workspace: $workspace, action: "readiness"}')
before_measurement_checks=$(run internet-improvement "$readiness_request")
while IFS= read -r test_case; do
  request=$(jq -c --argjson test_case "$test_case" \
    '. * $test_case.patch' <<<"$measurement_request")
  expected=$(jq -er '.expected' <<<"$test_case")
  if output=$("$statewright" internet-improvement "$request" 2>&1); then
    printf 'invalid polynomial measurement request succeeded: %s\n' "$request" >&2
    exit 1
  fi
  if ! jq -e --arg expected "$expected" \
      '.ok == false and (tostring | contains($expected))' <<<"$output" >/dev/null; then
    printf 'wrong polynomial rejection, expected %s: %s\n' "$expected" "$output" >&2
    exit 1
  fi
done < <(jq -cn '
  [
    {patch: {protocol_freeze_id: ""}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {inputs: []}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {inputs: "0"}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {inputs: [range(257) | "0"]}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {repetitions: 1}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {repetitions: 101}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {repetitions: -1}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {repetitions: 2.5}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {repetitions: "2"}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {inputs: [range(256) | "0"], repetitions: 17}, expected: "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID"},
    {patch: {inputs: [0]}, expected: "POLYNOMIAL_RATIONAL_STRING_REQUIRED"},
    {patch: {inputs: [""]}, expected: "POLYNOMIAL_RATIONAL_STRING_BOUNDS_INVALID"},
    {patch: {inputs: [("1" * 161)]}, expected: "POLYNOMIAL_RATIONAL_STRING_BOUNDS_INVALID"},
    {patch: {inputs: ["-"]}, expected: "POLYNOMIAL_RATIONAL_STRING_INVALID"},
    {patch: {inputs: ["1/"]}, expected: "POLYNOMIAL_RATIONAL_STRING_INVALID"},
    {patch: {inputs: ["1/-2"]}, expected: "POLYNOMIAL_RATIONAL_STRING_INVALID"},
    {patch: {inputs: ["1/2/3"]}, expected: "POLYNOMIAL_RATIONAL_STRING_INVALID"},
    {patch: {inputs: ["0.5"]}, expected: "POLYNOMIAL_RATIONAL_STRING_INVALID"},
    {patch: {inputs: [" 0"]}, expected: "POLYNOMIAL_RATIONAL_STRING_INVALID"},
    {patch: {inputs: ["1/0"]}, expected: "POLYNOMIAL_RATIONAL_DENOMINATOR_INVALID"},
    {patch: {inputs: ["0/0"]}, expected: "POLYNOMIAL_RATIONAL_DENOMINATOR_INVALID"}
  ] | .[]')
after_measurement_checks=$(run internet-improvement "$readiness_request")
jq -e --argjson before "$before_measurement_checks" \
  '.result.event_head == $before.result.event_head' <<<"$after_measurement_checks" >/dev/null

# A separate synthetic source exercises the real fetch/extract/translate path.
# It is not an internet discovery, an independent oracle, or approval evidence.
polynomial_root="$root/polynomial"
polynomial_watch=$(run internet-watch "$(jq -c \
  --arg workspace "$polynomial_root" --arg url "http://127.0.0.1:$port/polynomial" \
  '.workspace = $workspace | .canonical_url = $url' <<<"$watch_request")")
polynomial_policy_id=$(jq -er '.result.watch.source_policy_id' <<<"$polynomial_watch")
polynomial_schedule=$(run internet-poll "$(jq -cn --arg workspace "$polynomial_root" '{
  workspace: $workspace, action: "schedule",
  scheduled_interval: "2026-09-03T00:00:00Z",
  earliest_start: "2026-09-03T00:00:00Z", deadline: "2026-09-03T00:05:00Z",
  retry_ceiling: 1
}')")
polynomial_job_id=$(jq -er '.result.job_ids[0]' <<<"$polynomial_schedule")
polynomial_lease=$(run internet-poll "$(jq -cn --arg workspace "$polynomial_root" \
  --arg job_id "$polynomial_job_id" '{workspace: $workspace, action: "lease",
    job_id: $job_id, worker_id: "polynomial-fixture-worker",
    acquired_at: "2026-09-03T00:00:01Z", expires_at: "2026-09-03T00:01:01Z"}')")
polynomial_lease_id=$(jq -er '.result.lease_id' <<<"$polynomial_lease")
polynomial_capture=$(run internet-fetch "$(jq -cn --arg workspace "$polynomial_root" \
  --arg job_id "$polynomial_job_id" --arg lease_id "$polynomial_lease_id" '{
    workspace: $workspace, action: "execute", job_id: $job_id, lease_id: $lease_id,
    current_timestamp: "2026-09-03T00:00:02Z"}')")
polynomial_snapshot_id=$(jq -er '.result.snapshot_id' <<<"$polynomial_capture")
polynomial_fetch_id=$(jq -er '.result.fetch_receipt_id' <<<"$polynomial_capture")
polynomial_assessment=$(run internet-source "$(jq -cn --arg workspace "$polynomial_root" \
  --arg snapshot_id "$polynomial_snapshot_id" --arg fetch_receipt_id "$polynomial_fetch_id" \
  --arg source_policy_id "$polynomial_policy_id" '{workspace: $workspace, action: "assess",
    snapshot_id: $snapshot_id, fetch_receipt_id: $fetch_receipt_id,
    source_policy_id: $source_policy_id, robots_allowed: true,
    license_classification: "CC0-1.0"}')")
polynomial_assessment_id=$(jq -er '.result.assessment_id' <<<"$polynomial_assessment")
jq -e '.result.assessment.status == "SOURCE_ADMISSIBLE"' <<<"$polynomial_assessment" >/dev/null
polynomial_extraction=$(run internet-extract "$(jq -cn --arg workspace "$polynomial_root" \
  --arg snapshot_id "$polynomial_snapshot_id" '{workspace: $workspace,
    action: "execute", snapshot_id: $snapshot_id}')")
polynomial_extraction_id=$(jq -er '.result.extraction_receipt_id' <<<"$polynomial_extraction")
polynomial_feed_request=$(jq -cn --arg workspace "$polynomial_root" \
  --arg policy_assessment_id "$polynomial_assessment_id" \
  --arg extraction_receipt_id "$polynomial_extraction_id" '{workspace: $workspace,
    action: "feed", policy_assessment_id: $policy_assessment_id,
    extraction_receipt_id: $extraction_receipt_id,
    source_label: "synthetic local polynomial fixture, not qualification evidence", strict: true}')
polynomial_feed=$(run internet-improvement "$polynomial_feed_request")
jq -e '(.result.candidates | length) == 1 and
       .result.candidates[0].status == "VALIDATION_READY" and
       (.result.candidates[0] | tostring | contains("exact-polynomial-source-v1"))' \
  <<<"$polynomial_feed" >/dev/null
polynomial_readiness_request=$(jq -cn --arg workspace "$polynomial_root" \
  '{workspace: $workspace, action: "readiness"}')
polynomial_before_replay=$(run internet-improvement "$polynomial_readiness_request")
polynomial_replay=$(run internet-improvement "$polynomial_feed_request")
jq -e --argjson first "$polynomial_feed" \
  '.result.complete == true and .result.processed_fragments == 0 and
   (.result.candidates | length) == 0 and
   (.result.completion_output_ids | sort) == ($first.result.completion_output_ids | sort)' \
  <<<"$polynomial_replay" >/dev/null
polynomial_readiness=$(run internet-improvement "$polynomial_readiness_request")
jq -e --argjson before "$polynomial_before_replay" \
  '.result.event_head == $before.result.event_head' <<<"$polynomial_readiness" >/dev/null
jq -e '(.result.active_protocol_ids | length) == 0 and
       (.result.configuration_blockers | index("MISSING_EXPERIMENT_PROTOCOL")) != null' \
  <<<"$polynomial_readiness" >/dev/null

polynomial_candidate_id=$(jq -er '.result.completion_output_ids[] |
  select(startswith("internet-algorithm-candidate:"))' <<<"$polynomial_feed")
polynomial_candidate=$(jq -c '.result.candidates[0]' <<<"$polynomial_feed")
polynomial_ir_hash=$(printf '%s' "$(jq -cS '.proposed_saa_ir' <<<"$polynomial_candidate")" | sha256sum | awk '{print $1}')
polynomial_contract_hash=$(printf '%s' "$(jq -cS '.applicability.translation.execution_contract' <<<"$polynomial_candidate")" | sha256sum | awk '{print $1}')
polynomial_body_hash=$(jq -er '.result.artifact_bytes_id | sub("^artifact-bytes:sha256:"; "")' <<<"$polynomial_capture")
polynomial_baseline=$(run internet-improvement "$(jq -cn --arg workspace "$polynomial_root" \
  --arg candidate_id "$polynomial_candidate_id" --arg at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" '{
    workspace: $workspace, action: "capability-baseline", candidate_id: $candidate_id,
    recorded_at: $at}')")
polynomial_baseline_id=$(jq -er '.result.evidence_id' <<<"$polynomial_baseline")
# The groups below are frozen partitions of one synthetic fixture. They do NOT
# claim independent implementation, oracle lineage, reviewers, or qualification.
polynomial_protocol=$(jq -cn --arg candidate_id "$polynomial_candidate_id" \
  --argjson candidate "$polynomial_candidate" --arg baseline_ref "$polynomial_baseline_id" \
  --arg ir_hash "$polynomial_ir_hash" --arg contract_hash "$polynomial_contract_hash" \
  --arg body_hash "$polynomial_body_hash" '{
    protocol_version: "saa-grounded-polynomial-experiment-v1",
    applicable_candidate_statuses: ["VALIDATION_READY"],
    baseline_ref: $baseline_ref, baseline_saa_ir: {},
    dataset_snapshot_ids: [$candidate.snapshot_id],
    trial_groups: [
      {independence_group: "synthetic-fixture-anchors", deterministic_seed: 11,
       inputs: ["-1", "0"], expected_outputs: ["1", "-1"]},
      {independence_group: "synthetic-fixture-fractions", deterministic_seed: 29,
       inputs: ["1/2", "-1/2"], expected_outputs: ["-1/2", "-1/2"]}
    ],
    minimum_material_effect: "0", minimum_output: "-1", maximum_output: "1",
    valid_from: "2026-09-03T00:00:00Z",
    source_provenance: {grounded: {
      adoption_mode: "NEW_CAPABILITY", author_identity: "synthetic-cli-fixture",
      candidate_id: $candidate_id, candidate_ir_sha256: $ir_hash,
      source_fragment_id: $candidate.source_fragment_id, source_body_sha256: $body_hash,
      execution_contract_sha256: $contract_hash, measurement_repetitions: 2,
      claim: {inputs: $candidate.semantic_inputs, outputs: $candidate.semantic_outputs,
        units: $candidate.units, domain: "Exact rationals in [-1,1]",
        exclusions: ["Inputs outside the declared domain or resource limits"]},
      baseline_rationale: "Actual absence query of this disposable catalogue only",
      measurement_evidence_id: "", review_evidence_ids: [],
      limitations: ["Synthetic source and author-supplied expected values",
        "Groups are partitions, not evidence of independent lineage",
        "Shared GMP, compiler, host, coefficients and validation infrastructure",
        "No independent oracle review, benchmark scores or integrity observations"]
    }}
  }')
polynomial_freeze=$(run internet-improvement "$(jq -cn --arg workspace "$polynomial_root" \
  --argjson protocol "$polynomial_protocol" '{workspace: $workspace,
    action: "polynomial-protocol-freeze", protocol: $protocol}')")
polynomial_freeze_id=$(jq -er '.result.freeze_evidence_id' <<<"$polynomial_freeze")
jq -e '.result.qualification_claim == "NONE"' <<<"$polynomial_freeze" >/dev/null
polynomial_protocol=$(jq -c --arg freeze_id "$polynomial_freeze_id" \
  '.source_provenance.grounded.freeze_evidence_id = $freeze_id' <<<"$polynomial_protocol")
polynomial_registration=$(run internet-improvement "$(jq -cn --arg workspace "$polynomial_root" \
  --argjson protocol "$polynomial_protocol" '{workspace: $workspace,
    action: "protocol-register", protocol: $protocol}')")
polynomial_protocol_id=$(jq -er '.result.protocol_id' <<<"$polynomial_registration")
jq -e '.result.qualification_claim == "NONE" and
  .result.protocol.protocol_version == "saa-grounded-polynomial-experiment-v1" and
  (.result.protocol.source_provenance.grounded.review_evidence_ids | length) == 0' \
  <<<"$polynomial_registration" >/dev/null
polynomial_design=$(run internet-improvement "$(jq -cn --arg workspace "$polynomial_root" \
  --arg candidate_id "$polynomial_candidate_id" --arg freeze_id "$polynomial_freeze_id" '{
    workspace: $workspace, action: "polynomial-measurement-freeze",
    candidate_id: $candidate_id, protocol_freeze_id: $freeze_id,
    inputs: ["-1", "0", "1/2", "-1/2"], repetitions: 2}')")
polynomial_design_id=$(jq -er '.result.design_id' <<<"$polynomial_design")
polynomial_collect_request=$(jq -cn --arg workspace "$polynomial_root" \
  --arg design_id "$polynomial_design_id" '{workspace: $workspace,
    action: "polynomial-measurement-collect", design_id: $design_id}')
polynomial_measurement=$(run internet-improvement "$polynomial_collect_request")
jq -e '.result.qualification_claim == "NONE" and
  (.result.evidence_id | startswith("egcf-evidence:"))' <<<"$polynomial_measurement" >/dev/null
polynomial_before_collection_replay=$(run internet-improvement "$polynomial_readiness_request")
polynomial_measurement_replay=$(run internet-improvement "$polynomial_collect_request")
jq -e --argjson first "$polynomial_measurement" \
  '.result.evidence_id == $first.result.evidence_id' <<<"$polynomial_measurement_replay" >/dev/null
polynomial_after_collection_replay=$(run internet-improvement "$polynomial_readiness_request")
jq -e --argjson before "$polynomial_before_collection_replay" \
  '.result.event_head == $before.result.event_head' <<<"$polynomial_after_collection_replay" >/dev/null
polynomial_measurement_id=$(jq -er '.result.evidence_id' <<<"$polynomial_measurement")
polynomial_measurement_check_request=$(jq -cn --arg workspace "$polynomial_root" \
  --arg evidence_id "$polynomial_measurement_id" --arg freeze_id "$polynomial_freeze_id" \
  --arg at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" '{workspace: $workspace,
    action: "polynomial-measurement-check", evidence_id: $evidence_id,
    protocol_freeze_id: $freeze_id, recorded_at: $at}')
polynomial_measurement_check=$(run internet-improvement "$polynomial_measurement_check_request")
jq -e '.result.status == "RAW_MEASUREMENT_CHAIN_VALIDATED" and
  .result.qualification_claim == "NONE" and .result.admission == false and
  (.result.performance | type) == "object"' <<<"$polynomial_measurement_check" >/dev/null
for failure in wrong-evidence early-cutoff; do
  if [[ $failure == wrong-evidence ]]; then
    invalid_check=$(jq -c --arg design_id "$polynomial_design_id" \
      '.evidence_id = $design_id' <<<"$polynomial_measurement_check_request")
    expected=POLYNOMIAL_MEASUREMENT_CHAIN_EVIDENCE_INVALID
  else
    invalid_check=$(jq -c '.recorded_at = "2000-01-01T00:00:00Z"' \
      <<<"$polynomial_measurement_check_request")
    expected=POLYNOMIAL_MEASUREMENT_CHAIN_BINDING_OR_ORDER_INVALID
  fi
  if output=$("$statewright" internet-improvement "$invalid_check" 2>&1); then
    printf 'invalid polynomial measurement chain succeeded: %s\n' "$failure" >&2
    exit 1
  fi
  jq -e --arg expected "$expected" '.ok == false and (tostring | contains($expected))' \
    <<<"$output" >/dev/null
done
polynomial_after_chain_checks=$(run internet-improvement "$polynomial_readiness_request")
jq -e --argjson before "$polynomial_after_collection_replay" \
  '.result.event_head == $before.result.event_head' <<<"$polynomial_after_chain_checks" >/dev/null
polynomial_check=$(run internet-improvement "$(jq -cn --arg workspace "$polynomial_root" \
  --arg protocol_id "$polynomial_protocol_id" --arg at "$(date -u +%Y-%m-%dT%H:%M:%SZ)" '{
    workspace: $workspace, action: "protocol-check", protocol_id: $protocol_id,
    recorded_at: $at}')")
jq -e '.result.status == "PROTOCOL_BLOCKED" and .result.admission == false and
  (.result.blocker | contains("SUPPORTED_GROUNDED_PROTOCOL_REQUIRED") | not)' \
  <<<"$polynomial_check" >/dev/null
polynomial_final_candidates=$(run internet-candidate "$(jq -cn --arg workspace "$polynomial_root" \
  '{workspace: $workspace, action: "list"}')")
jq -e '(.result.candidates | length) == 1 and
  .result.candidates[0].payload.status == "VALIDATION_READY" and
  (.result.candidates[0].payload.experiment_qualification_ids | length) == 0 and
  (.result.candidates[0].payload.probation_admission_ids | length) == 0' \
  <<<"$polynomial_final_candidates" >/dev/null

printf 'StateWright internet CLI smoke passed: legacy lifecycle, 21 polynomial rejection checks, synthetic source and measurement replay, qualification still blocked; no live internet\n'
