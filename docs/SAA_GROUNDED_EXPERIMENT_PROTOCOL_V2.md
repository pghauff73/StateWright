# Grounded SAA experiment protocol v2

## Boundaries

Protocol version `saa-grounded-experiment-v2` uses the existing immutable
`internet-experiment-protocol` record and its `source_provenance.grounded`
envelope. Existing signatures and legacy record layouts remain readable.
Grounded qualification is opt-in per store. Production stores should require it;
legacy fixture qualification must not be described as independently reviewed.

Two adoption modes are implemented:

- `REPLACEMENT`: compare mean absolute error with a captured, deployed baseline.
  Its evidence must contain `kind: DEPLOYED_BASELINE_CAPTURE_V1`, `saa_ir`, and a
  deployment reference. Independent reviewers attest that this is the actual
  operational baseline, not an intentionally defective example.
- `NEW_CAPABILITY`: compare correct supported fraction against a recorded
  unsupported lookup in this store's canonical catalogue. No numeric baseline
  IR is invented. The baseline IR must be an empty object. The independent
  correctness oracle is stored separately. This is not a claim of project-wide
  novelty, nor a comparison with all possible runtime fallbacks.

Both modes retain exact-output, benchmark, integrity, source freshness,
independence, promotion, canary and probation gates. Negative controls are
required review evidence. Trial cases are never production probation uses.
For an unsupported baseline, selection outside the canary returns an empty
`selected_canonical_ref` and `BASELINE_UNSUPPORTED_NO_CANONICAL_FALLBACK`, not an
attempt to execute an evidence record.

## Local reviewer trust

The operator maintains `.ourd-agent/egcf/experiment-trust.json` inside each store:

```json
{
  "schema_version": 1,
  "require_grounded_protocols": true,
  "allowed_adoption_modes": ["REPLACEMENT", "NEW_CAPABILITY"],
  "reviewer_public_keys": {}
}
```

An empty key map grants no review authority. Keys are lowercase hex Ed25519
public keys, indexed by reviewer identity. The assistant must not invent reviewer
identities or production key pairs. The store owner authenticates reviewers and
installs their public keys out of band. A local store owner can change this trust
file; this is not a security boundary against a malicious local administrator.

At least two distinct reviewers, public keys, experiment-group identities and
methods are required. Reviewers must differ from the protocol author. Shared
sources and dependencies are disclosed, not counted as independent publishers.
Cryptography authenticates attestations; it cannot prove intellectual independence
or that a human's claims are truthful. Those remain review responsibilities.
Current keys, allowed adoption modes and protocol validity are checked again at
promotion. Removing a key prevents a fresh promotion assessment using that key.

## Grounded envelope

`source_provenance.grounded` binds:

- `adoption_mode`, `author_identity`, `candidate_id`, `candidate_ir_sha256`;
- `source_fragment_id`, `source_body_sha256`;
- `claim` containing `inputs`, `outputs`, `units`, `domain`, `exclusions`;
- `baseline_rationale` (exactly `CANONICAL_CATALOG_LOOKUP_ONLY` for new capability);
- `reference_oracle_ir`, derived independently from the retained source;
- `measurement_evidence_id` for recorded benchmark scores and integrity snapshots;
- `review_evidence_ids` for signed independent review envelopes.

The normal protocol fields freeze trials, input/output bounds, thresholds,
benchmark and integrity policies, validity dates and source dataset references.
V2 trial inputs and expected outputs are exact rational strings. Reusing a
protocol while changing its request data or policies is rejected. Context hashes
bind group identities, seeds, inputs and expected outputs.

Review signatures cover canonical JSON for this message:

```json
{
  "reviewer_id": "external-reviewer-identity",
  "protocol_binding_sha256": "binding returned by the native CLI",
  "verdict": "APPROVE",
  "reviewed_at": "canonical UTC timestamp",
  "independence_group": "group represented in the frozen trials",
  "method": "independently implemented reference method",
  "derivation": "source-bound derivation and reproduction findings",
  "shared_dependencies": ["source and tool dependencies"],
  "negative_control_evidence_ids": ["immutable evidence references"]
}
```

The envelope is `{message: ..., signature_hex: ...}`. The native protocol binding
excludes the protocol's signature and review IDs to avoid a circular hash, but
includes all experiment settings and the evidence that reviewers must assess.
Changing the candidate, source, oracle, measurements or baseline requires new
review signatures. Do not sign a packet containing placeholders.

## Native operations

All operations below use `internet-improvement` and an explicit store workspace:

1. `capability-baseline`: provide `candidate_id`, `recorded_at`. Records an actual
   catalogue query only if no exact semantic/structural candidate is found.
2. `protocol-binding`: provide the complete proposed `protocol`. Returns the
   content hash for independent review; does not approve or register it.
3. `review-register`: provide `candidate_id`, `recorded_at`, signed `review`.
   Verifies the signature against local reviewer keys before recording evidence.
4. `protocol-register`: provide the completed protocol and review evidence IDs.
5. `protocol-check`: provide `protocol_id`, `recorded_at`. Checks grounding and
   returns `PROTOCOL_EVIDENCE_READY` or a blocker. It is not a qualification run.
6. `protocol-qualify`: provide `protocol_id`, `recorded_at`. Executes the registered
   protocol through the exact internal evaluator and records its outcome.

The native orchestrator passes protocol identity into qualification as well.
With `require_grounded_protocols: true`, unbound and legacy requests cannot bypass
these checks, and legacy qualifications cannot obtain new promotion assessments.
Other stores retain their existing behavior until their owner configures trust.

## Repair v5 rollout

Build and focused tests precede source reprocessing. `reprocess` takes an explicit
historical policy assessment and creates fresh extraction/feed evidence. Old
records are kept, and the director skips unqualified legacy fragments once a
current-version extraction of their snapshot exists. Existing canonical and
probationary lifecycles are not discarded by extraction-version changes.

The licensing review queue is a set of pending decisions, not an approval list.
Independent review and measured production observations remain prerequisites;
missing values must not be filled with synthetic experiment fixture results.
