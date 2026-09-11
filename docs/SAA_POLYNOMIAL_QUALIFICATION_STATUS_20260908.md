# Polynomial qualification status: 2026-09-08

## Outcome

Implementation has advanced, but the requested end-to-end qualification goal
is not complete. No production polynomial approval is established by this work.
Execution, sampled agreement, source admissibility, independent review and
acceptance are distinct claims.

## Latest observed validation

- The internet-filtered native suite passed 120 test cases and 2,141 assertions.
  Log: `/tmp/statewright-reference-protocol-queue-tests.log`.
- The CLI internet smoke test passed after separating the reference evaluator
  from the new-capability baseline. This predates the latest protocol-link
  changes and is not a claim that those later changes passed the CLI test.
  Log: `/tmp/statewright-reference-oracle-cli-tests.log`.
- Scheduler tests exercise bounded comparison execution, store reopening,
  active/expired lease recovery, duplicate receipt rejection and scope checks.
  Protocol-bound scheduled execution checks the link in start/result receipts.
- A bounded invocation of the existing production worker completed one action
  with zero failures when the comparison stage was introduced. This was an
  exploratory comparison, not a qualification experiment.
  Log: `/tmp/statewright-reference-live-supervisor.jsonl`.

These are scoped observations, not a full requirement-by-requirement completion
audit or proof of independent numerical correctness.

## Actual source and exploratory evidence

Store workspace:
`build/saa-feed-100-20260905-EqQArB/store`.

The selected source is the already acquired Fungrim Chebyshev document at
revision `b7c3ca6e565e1058638cab6ba2bc811090296767`. The narrow proposal is
`T_2(x) = 2*x^2 - 1` on the declared rational subdomain.

| Record | Native ID |
| --- | --- |
| Source snapshot | `internet-source-snapshot:sha256:7974346715cb3806b29271c978ca4a4c39c7abb99dcae57c8b210bd2a9ae34d8` |
| Source policy assessment | `internet-policy-assessment:sha256:08137d68ac7fd7e86de3393fada95ca3b498b4169833cc6aa47f7d57e36e6f86` |
| Translation proposal | `egcf-evidence:sha256:65b270864fb6edf9c18668fbe83aaa449a323828c9a09419cd14861d84ce2bc7` |
| Frozen reference design | `egcf-evidence:sha256:900d9e7c4125a7b8212f0e16ecfd6da182155241ad6a081ae6212a0307cab735` |
| Exploratory comparison | `egcf-evidence:sha256:71a124fa97c659726c343012e102cebc224d3cffa9fa5778ec97f4dc5b85f76e` |

The seven inputs were `-1`, `0`, `1`, `-1/2`, `1/2`, `-1/3`, `1/3`.
All recorded outputs matched the integer-recurrence reference. The receipt
explicitly reports `independence_status: NOT_ESTABLISHED` and
`qualification_claim: NONE`. It must not be retrospectively relabelled as a
post-protocol-freeze experiment.

## Implemented mechanisms relevant to the next experiment

- Versioned exact-rational Horner execution supports fixed coefficients and
  degree 0 through 16, with domain and resource checks. Existing scalar and
  affine execution paths remain separate.
- The source-bound Fungrim proposal retains context and review requirements;
  downloaded source is treated as inert data.
- A separate int64 Chebyshev recurrence supplies reference values on its bounded
  input subset. GMP remains a transport type at the qualification boundary.
  Shared author, compiler and host dependencies remain disclosed.
- Polynomial protocol design freezing retains source, thresholds, policies,
  oracle definition and trial data. Later comparison-result IDs are outcomes,
  not changes to the frozen design.
- Qualification using the integer reference requires a matching native design,
  protocol-linked start/result receipts, consistent source and trial bindings,
  no sampled mismatches and valid chronology.
- These evidence checks do not grant mathematical-review authority, establish
  independent experiment groups or prove a uniform error bound.
- New-capability experiments retain the recorded catalog-absence baseline.
  They no longer execute the oracle as if it were a deployed implementation.
  Replacement experiments still require a captured deployed baseline.
- The existing rotation has a bounded reference-comparison stage, preserving
  its worker identity, locking and phase cursor. Protocol-freeze mappings can
  be supplied per design. The current production queue remains exploratory.

## Newly confirmed external review gap

A read-only inspection of
`store/.ourd-agent/egcf/experiment-trust.json` found:

- `require_grounded_protocols: true`.
- Both `REPLACEMENT` and `NEW_CAPABILITY` are allowed adoption modes.
- `reviewer_public_keys` has zero entries.

The current signed-review path requires two distinct trusted reviewers and
keys, distinct methods/groups, author separation, protocol-bound approvals and
negative-control evidence. Consequently it cannot authorize this polynomial
protocol with the current trust configuration. The CSS automated-review mode
is explicitly excluded for polynomial candidates.

Registering keys invented by the implementation author, or changing reviewer
labels while retaining the same control and lineage, would not resolve
independence. No reviewer trust was added or relaxed.

## Measurement requirements that remain unresolved

The inspected OIEC gate checks seven supplied track scores and associated
evidence: TRUTHGROUND, MEANINGPATH, SEMANTICREP, MEANINGGROUND, WORKGROUND,
PROGRESSCERT and AGENTWORK. That gate is not itself a polynomial measurement
collector and does not define a defensible conversion from raw timings into
those seven scores.

The inspected longitudinal integrity model requires observations of:

- Canonical knowledge counts, semantic contradictions and semantic drift.
- False canonical admissions.
- Corrected-error opportunities and recurrences.
- Retrieval queries and correct selections.
- Equivalent-failure opportunities and retries.

Several counters require independently justified judgments about correctness.
Store-file counts or absence of recorded failures cannot establish those
judgments. Missing measurements must remain unavailable, not default to zero
failures or perfect precision. Successive snapshots must arise from real
observation periods, not generated timestamps around a single measurement.

## Remaining work and required decisions

1. Establish the approved benchmark specification for this narrow polynomial
   capability: workloads, judged outcomes, scoring rules, uncertainty and
   thresholds. Freeze it before qualification experiments.
2. Identify independently controlled reviewer identities/services and their
   authorized public keys, or specify a defensible independently checkable
   review system. This requires an authority decision, not new labels.
3. Complete mathematical source-context review and connect the Fungrim proposal
   to native candidate progression without rewriting historical quarantine.
4. Register the actual candidate-bound polynomial protocol and defensible
   baseline. Freeze fresh experiment groups before their evaluation; do not
   reuse the seven exploratory observations as held-out evidence.
5. Collect the required benchmark and longitudinal observations, retaining raw
   evidence and provenance. Complete independent review and negative controls.
6. Exercise real qualification, probation and subsequent observations. Do not
   report accepted status from compilation, receipt presence or sampled matches.
7. Reassess a higher-level candidate only after its actual dependencies and
   composed-error requirements have been satisfied. That milestone has not been
   demonstrated for Airy functions.

The implementation goal remains active and incomplete. The missing review
authority and benchmark specification are explicit dependencies; neither is
permission to weaken acceptance gates.
