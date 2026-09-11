# Source-bound polynomial candidate registration

## Scope

This slice registers only Fungrim Chebyshev T2, `2*x^2 - 1`, using the existing
exact-rational Horner family on its declared bounded domain. It does not
translate general polynomial families, grant mathematical review, create
experiment results, or admit a canonical function.

The implementation is not yet built, tested, or deployed. Existing binaries
and recurring configuration are unchanged. Do not treat this document as
evidence that the new path has executed successfully.

## Native entry points

The `internet-improvement` actions `polynomial-candidate-inspect` and
`polynomial-candidate-register` require `workspace`, `proposal_evidence_id`, and
`retrieval_receipt_id`. Supply real native IDs, not downloaded documents or
handwritten retrieval results. Inspection performs no registration.

Registration validates the immutable proposal against its original fragment,
snapshot bytes, source notices and admissible assessment. The retrieval must
be a complete native receipt for the same fragment, snapshot and assessment,
linked to a native brain-feed batch. Conflicting source-assessment history
fails closed and requires explicit resolution.

The source-bound translator is shared with qualification's faithful-source
verification. No synthetic scalar procedure is inserted into the source.
Existing scalar, affine and explicit-polynomial translators remain unchanged.

An initial registration is always QUARANTINED with mathematical-context and
domain/branch/error-bound review requirements. It preserves the proposal ID,
retrieval history and original quarantine records. It makes no new novelty
claim. No historical record is superseded merely because registration succeeds.

Identical requests return the same immutable registration candidate ID without
writing another candidate. Conflicting retrieval bindings for the same proposal
are rejected using a bounded lookup. Subsequent lifecycle records are preserved;
the returned registration ID and initial status are not a current lifecycle
verdict. A lookup-budget failure is insufficient evidence, not permission to
register a duplicate.

## Optional existing-rotation queue

The existing batch helper source at
`build/saa-feed-100-20260905-EqQArB/supervisor/advance-requested-urls.cpp`
accepts an optional top-level `polynomial_candidate_queue` in its recurring
request. Each entry contains exactly `proposal_evidence_id` and
`retrieval_receipt_id`. At most 32 unique proposals are permitted. No entries
were added by this implementation.

The rebuilt helper validates the explicit queue and defers for unexpired
native leases. Under the existing native store lock, at most one missing
candidate registration consumes a translation slot. The content-addressed
registration itself is the durable completion checkpoint; a retry skips it.
Once the queue is registered, ordinary Boost translation continues. The
qualification slot includes only source-verified candidates from the explicit
queue in addition to the unchanged 100-URL scope. It still uses the existing
director/orchestrator, worker, reasoning requirement, native action leases,
protocol selection and promotion gates. No new timer or worker is created.

Because this helper lives in batch build artifacts, preserve its source when
recreating that batch. Enabling the queue requires rebuilding the helper and
CLI and explicitly supplying the real proposal/retrieval bindings. Neither
deployment nor live-store registration is performed by this source edit.

## Protocol preparation and remaining evidence

The registration response supplies the candidate's execution contract and
polynomial protocol family, plus an explicitly unassessed prerequisite list.
Use the existing `polynomial-protocol-freeze` and protocol-registration path
only after supplying a complete candidate-bound design and defensible baseline.
No protocol is generated or frozen by candidate registration.

Still required: mathematical review, authorized independent reviewers, approved
benchmark workloads and score mapping, fresh frozen independent experiment
groups, negative controls, genuine measurements and longitudinal observations.
The earlier seven exploratory comparisons cannot become held-out post-freeze
evidence retrospectively. Missing measurements remain missing.

## Validation to run when authorized

- Register an actual source-bound proposal and verify faithful translation.
- Reject tampered proposal content, source hashes, coefficients, domains and notices.
- Reject mismatched or fabricated retrieval receipts and conflicting policy history.
- Repeat registration and resume after an interrupted write without duplicate candidates.
- Preserve advanced lifecycle records when replaying initial registration.
- Verify that absent reviews and measurements cannot yield qualification.
- Exercise explicit queue scope, lease deferral and per-slot limits.
- Check existing scalar and affine paths for regressions.

No tests were added or run as part of this source-only slice.
