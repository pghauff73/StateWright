# Polynomial probation integration: evidence and required changes

Status: implementation incomplete. This document does not authorize admission,
change historical records, or supply qualification evidence.

## Recovered test evidence

The completed internet-name-filtered regression run recorded in
`/tmp/statewright-polynomial-baseline-tests-2.log` reports 1,056 assertions
passing in 97 test cases. The original process handle is no longer available.
The result establishes that this recorded run passed, not that subsequent
changes or the recurring deployment have been tested. The log is a local
development artifact, not an immutable native experiment receipt.

## Inspected integration constraints

1. `InternetProbationController::admit` constructs a linear canonical bundle,
   admits it through `CanonicalAlgorithmStore`, and exposes a
   `CanonicalAdmissionResult`. Polynomial execution alone cannot satisfy this
   representation contract.
2. `canonicalize_taylor_jet` and the nonlinear search interface require a
   `CanonicalRepresentativeAlgorithmForm` parent. A fabricated zero-slope
   parent would misrepresent the polynomial rather than resolve this dependency.
3. `max_jet_order` is four. Exact symbolic acquisition separately enforces
   orders one through four. Raising one constant would not implement complete
   degree-16 representation, resource controls, storage or qualification.
4. `acquire_exact_polynomial_jet` certifies coefficients only to the requested
   truncation order. Its warning explicitly disclaims global equality to the
   truncated jet. Existing remainder APIs must not be confused with a complete
   internet admission path.
5. That acquisition routine sets `independent_acquisition = true` internally.
   Its source hash identifies the supplied polynomial-system payload. Neither
   fact establishes independent source provenance, expected-result derivation,
   experiment groups or reviewer lineage for an internet candidate.
6. Existing probation selection and observation operate on admission references,
   but the admission result and canonical-store write remain representation
   specific. Their apparently generic references do not prove that downstream
   retrieval or execution accepts a polynomial object.

## Implementation decision

Introduce a separately versioned, full-coefficient exact polynomial
representation for the internet family. Do not make a truncated local Taylor
jet the identity of a degree-16 function. Preserve existing linear and local
nonlinear records and their signature material.

The representation must bind all coefficients, exact numeric semantics,
declared input domain, semantic contract and execution resource policy. Keep
mathematical behavior identity distinct from implementation/provenance identity:
equivalent Horner and power-sum evaluation can describe the same mathematical
function while remaining different execution artifacts and evidence lineages.
Any normalization of trailing zero coefficients must be explicit and versioned;
it must not silently alter source or execution artifact hashes.

## Ordered implementation work

1. Add full-polynomial representation construction, strict decoding and stable
   signatures. Cover degrees zero through sixteen, rational normalization,
   invalid coefficients, domain and budget binding, and distinct nonlinear
   functions that agree at selected samples.
2. Add a versioned canonical storage/admission path with native immutable
   records and locking. Validate candidate, qualification, policy and
   representation bindings before storage. Reuse existing lifecycle policy,
   not an unqualified linear placeholder.
3. Extend probation admission results and their consumers through an explicit
   representation discriminator. Preserve legacy serialization and signatures
   when the representation is linear. Audit retrieval, canary selection,
   observation, promotion, demotion and replay before enabling admission.
4. Add execution-backed observation support. Compare actual selected artifacts
   against bound expected results and record actual environment, failures and
   timings. Do not convert historical fixture results into operational windows.
5. Complete qualification consumers before enabling source-bound translation:
   polynomial-specific frozen protocols, independent expected-result lineage,
   equivalent baseline, measured benchmark evidence, trusted review and
   operational integrity requirements. The symbolic-acquisition flag must not
   satisfy independent experiment or reviewer requirements.
6. Add source-bound translation and then exercise the complete native lifecycle
   using controlled test fixtures. Keep those fixtures distinct from actual
   source review, benchmark and acceptance evidence.
7. Integrate into the existing bounded recurring worker only after native
   consumer tests pass. Test interruption/resumption, duplicate prevention and
   stale evidence handling without starting a second worker.

## Required regression and adversarial coverage

- Historical identity, constant and affine receipt/signature compatibility.
- Full degree-16 representation, including terms above Taylor order four.
- No promotion based on pointwise agreement or a truncated jet alone.
- Rejection of an unrelated parent representation and stale contract hashes.
- No independent-review credit from an internally assigned acquisition flag.
- No probation without genuine qualification and current promotion evidence.
- Nonlinear retrieval, canary fallback, demotion and immutable replay.
- Durable recovery after interruption at each admission boundary.

## Remaining evidence boundary

No polynomial candidate acceptance, source authorization, reviewed protocol,
deployment or operational observation is established by this inspection.
The active implementation goal remains open. The immediate engineering work is
the complete representation and admission path; independent review and real
measurement collection remain separate qualification requirements.
