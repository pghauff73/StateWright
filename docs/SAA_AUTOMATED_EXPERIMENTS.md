# Automated CSS experiment and review

## URL to acceptance

1. Register the URL and evaluate source licensing and acquisition policy. An
   algorithm experiment does not grant permission to ingest a restricted source.
2. Fetch and retain the immutable source snapshot and source-policy assessment.
3. Extract complete procedure context. Document truncation is distinct from
   completeness of a particular source section.
4. Classify and translate. Headers and incidental mentions are not executable
   candidates. Unsupported algorithms remain unsupported, not failed experiments.
5. Freeze a source-bound experiment, domain, exclusions, baseline and thresholds.
6. Execute correctness experiments, benchmark and integrity measurements; review
   their evidence. The CSS review path below does not require human signatures.
7. Qualify through the existing coordinator, then assess the promotion policy.
8. Admit to probation only with its native receipt. Collect actual operational
   observations before canonical acceptance; regression and rollback remain active.

## Implemented machine authority

### Durable pre-registration and execution

The lifecycle now requires a persisted design before the candidate runs:

1. `automated-css-design` returns source-bound fixtures and limits without
   executing the candidate. This is a design preview, not experimental evidence.
2. `automated-css-freeze` takes a grounded `protocol`, `candidate_id` and
   `recorded_at`. It performs a real local catalogue lookup, records the baseline,
   and freezes the source, candidate, fixtures, domain, policies and thresholds.
   Use the returned protocol, which includes the new baseline and freeze IDs.
3. `automated-css-experiment` additionally requires `freeze_evidence_id`. It
   validates the persisted design and records an actual run. Failed comparisons
   and execution errors are stored with `success: false`, not discarded.
4. `automated-css-review-register` requires the returned frozen protocol,
   `freeze_evidence_id`, `experiment_evidence_id`, and the independently collected
   benchmark/integrity measurement reference inside the protocol. It replays the
   candidate, compares the run receipt and frozen design, and binds machine review
   to the completed protocol. It does not ask a person to approve.
5. Run native qualification and promotion. Neither a freeze, a passing run, nor a
   machine-review receipt alone constitutes acceptance.

The freeze excludes only result fields and eventual evidence references from its
design binding; changing policies, inputs, expected results, source, oracle or
claim scope requires a new freeze. Persisted IDs allow a caller to resume at the
next stage without reacquiring a URL. Each run has a fixed sixteen-trial budget.
These lifecycle controls do not by themselves establish separate-host provenance
or provide a wall-clock limit for store I/O. Existing native supervisor deadlines
remain necessary. Older receipts must be rerun through the frozen workflow.

`internet-improvement` action `automated-css-experiment` takes `workspace`,
`candidate_id`, and `recorded_at`. It records reproducible experiment evidence for
the pinned, complete W3C CSS Values 3 absolute-length section. It supports in, cm,
mm, Q, pc and pt to CSS pixels, and executes no downloaded code.

The reference derives conversions from units per inch and 96 CSS pixels per inch,
not the candidate's slope. Exact coefficient equality on the verified closed affine
graph establishes rational-domain equality. A separate bounded-integer/GCD
reference checks GMP scalar execution on two disjoint groups of eight signed,
zero, integer and fractional inputs. Three deliberately wrong implementations
(scale, bias and sign mutations) must all be detected. Evidence contains expected
and actual results, source and IR bindings, limits and shared dependencies.
Integer anchors now use a separately expressed normative rational conversion
table; fractional cases use units-per-inch cross-products. This avoids labeling
two partitions of a single reference calculation as independent methods.

These are different methods, not independent people or organizations. They share
the pinned specification, host and toolchain. A shared interpretation error in
the specification remains a limitation. No performance superiority is claimed.

Action `automated-css-review-register` additionally takes a complete `protocol`.
Use the experiment's exact `trial_groups`, a genuine local catalogue absence
baseline, and actual benchmark/integrity evidence. The action binds machine review
to the frozen protocol and registers it. It does not manufacture missing benchmark
scores, integrity history, or probation uses. Registration is not qualification.
Use the existing `protocol-check`, `protocol-qualify` and promotion workflow next.

The protocol remains grounded v2 and selects `grounded.review_mode` equal to
`AUTOMATED_CSS_V1`. This deliberately replaces the two-human-signature requirement
with a closed-family, replayable machine authority, not fabricated reviewer keys.
Qualification and promotion both rerun and compare the exact experiment receipt.
Only NEW_CAPABILITY is supported; catalogue absence is local, not global.
The trust file may set `allow_automated_css_review` to false to disable this
authority. Existing allowed adoption modes and signed-review paths remain intact.

## Remaining work and efficacy limits

This change supplies correctness experiments and machine review. It does not yet
provide collectors for all existing benchmark tracks or longitudinal integrity
metrics, auto-generate a complete grounded protocol, or wire experiment discovery
into recurring acquisition. Those missing measurements remain genuine blockers,
not requests for human approval. Evidence collection must measure the actual
track, rather than map sixteen conversion successes to unrelated perfect scores.

No live acceptance is implied by installing this code. Experiment executions are
not production uses. Unsupported source revisions and algorithm families fail
closed. Operational probation, source freshness, policy and rollback gates have
not been removed.
