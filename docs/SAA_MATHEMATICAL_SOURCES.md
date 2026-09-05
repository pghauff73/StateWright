# Mathematical sources replacing the NIST DLMF lane

## Configuration and license review

The current configuration is `watchlists/saa-all-lanes-100-v2.json`: 20 Crossref,
20 Europe PMC, 20 RFC Editor, 20 W3C, 10 Fungrim and 10 Boost.Math URLs. NIST
is absent from this replacement manifest. Prior manifests, disabled registrations
and immutable feed history are retained, not rewritten. This does not approve
the old DLMF lane or resolve the Crossref/Europe PMC license reviews.

| Source | Immutable revision | Reviewed scope |
| --- | --- | --- |
| Fungrim | `b7c3ca6e565e1058638cab6ba2bc811090296767` | Ten Python formula-data files, MIT |
| Boost.Math | `5e088ffe2ed0e237b9069e3a7352865283d8f196` (Boost 1.89.0) | Ten QuickBook documentation files, BSL-1.0 |

Fungrim topics: beta, Chebyshev, error functions, exponential, gamma, Gaussian
quadrature, Legendre polynomials, logarithm, sine and square root.

Boost.Math topics: error measurement, barycentric rational interpolation,
cardinal cubic B-splines, modified Akima interpolation, PCHIP, Gauss quadrature,
Gauss–Kronrod quadrature, trapezoidal quadrature, derivative-based root finding
and derivative-free root finding.

The registry in `resources/watchlists/internet/source-groups-v1.json` records
each exact URL and SHA-256, the review date and scope, and full license notices
with their own hashes. Review consisted of checking the project license and
each selected file's notices, overrides and external-material references.
Each selected Boost file explicitly carries its license and author copyright.
Fungrim's project MIT notice covers the selected formula-data files.

License evidence:

- [Pinned Fungrim MIT notice](https://raw.githubusercontent.com/fredrik-johansson/fungrim/b7c3ca6e565e1058638cab6ba2bc811090296767/LICENSE)
- [Pinned Boost.Math license](https://raw.githubusercontent.com/boostorg/math/5e088ffe2ed0e237b9069e3a7352865283d8f196/LICENSE)

Only these files are approved as inert quoted data with their notices retained.
Referenced papers, websites, image assets, imported examples and dependencies
are **not** approved by implication. Includes/imports and image macros are not
expanded. Future revisions or extra files need another file-level review and
fresh preflight. Redistribution must carry the applicable notices; the review
does not remove upstream obligations.

## Extraction and admission boundaries

`mathematical-source-v1` accepts only a reviewed `text/plain` file matching its
pin. Manifest validation binds the review to the trusted registry and an
immutable URL. Preflight hashes the response; the fetch coordinator checks again
before snapshot capture, and extraction checks again before emitting fragments.
Changed bytes, missing notices, altered review evidence and unreviewed paths fail
closed. Registration provenance carries the complete review and license notices.

One complete file becomes one byte-bound fragment. Leading whitespace, final
newline, expressions, assumptions, branch conventions, accuracy discussions,
references and author copyright are retained verbatim. Oversized files are
rejected rather than truncated into misleading formula-only fragments.

Preservation is not interpretation: external definitions remain unresolved and
conditions are labelled `PRESERVED_UNINTERPRETED`. Mathematical fragments receive
`MATHEMATICAL_CONTEXT_REVIEW_REQUIRED` and cannot pass through the existing
identity/affine translator even if an example resembles a supported procedure.
Full special-function/complex-domain qualification is not implemented by this
change. No downloaded Python or C++ is executed.

## Optional numerical verification

`Tools/verify_mathematical_candidates.py` is a separate offline pointwise checker,
not a new runtime dependency of StateWright's native ingestion or qualification.
It uses mpmath 1.4.1 and python-flint 0.9.0 with its bundled FLINT 3.6.0/Arb.
The exact wheels are SHA-256 pinned in `Tools/math-verification-requirements.txt`
for CPython 3.14 on Linux x86_64. Installed package notices are retained and
inventoried by hash in each report, including bundled FLINT/GMP/MPFR licenses.
Do not copy linked library code into SAA without satisfying its license terms.

```sh
python3 -m venv build/math-verification-venv
build/math-verification-venv/bin/python -m pip install \
  --require-hashes --only-binary=:all: --no-deps \
  -r Tools/math-verification-requirements.txt
build/math-verification-venv/bin/python Tools/verify_mathematical_candidates.py \
  Tests/fixtures/mathematical-points-v1.json NEW_REPORT.json
build/math-verification-venv/bin/python Tests/test_mathematical_verification.py
```

Input cases explicitly name a function, decimal input, proposed decimal output
and absolute error budget. Supported functions: exp, log, sqrt, sin, cos, gamma,
erf and zeta. Inputs are real and bounded in magnitude by 100; log/gamma require
positive input, sqrt uses the nonnegative principal root and zeta requires x ≥ 2.
Complex branches, gamma poles and arbitrary expressions are rejected, not inferred.
The maximum permitted absolute error budget is 1e-12.

mpmath checks at 80 and 120 decimal digits, including precision stability. Arb
computes at 384 bits and must prove a directed absolute-error upper bound fits
within the proposed budget; interval overlap alone does not pass. Insufficient
precision can conservatively fail a valid value. A pass covers only the tested
point, not a whole domain, implementation, or derivation of its error bound.
The report states `algorithm_qualification: NOT_PERFORMED`. Shared algorithmic
ancestry is acknowledged; the libraries are not counted as independent proofs.

Optional CTest integration:

```sh
cmake -S . -B build \
  -DSTATEWRIGHT_MATH_VERIFY_PYTHON="$PWD/build/math-verification-venv/bin/python"
ctest --test-dir build -R mathematical_verification --output-on-failure
```

## Pilot and deployment

The bounded replacement-lane pilot uses `watchlists/saa-mathematics-20-v1.json`,
an isolated store at `build/mathematics-pilot-store-v1`, and evidence in
`build/mathematics-pilot-evidence-v1`. This avoids disturbing the earlier standards
feed holding the project store. These pilot records are not project-store records.
Do not submit the full 100-entry replacement to the one-time feeder against an
existing store: it deliberately refuses ownership of pre-existing watches.

For a new acquisition pilot (this downloads sources), after the store is idle:

```sh
./build/statewright_feed_watchlist_once . ./resources \
  ./watchlists/saa-mathematics-20-v1.json ./build/NEW_MATH_FEED_EVIDENCE
```

The output directory must be new. This runs normal robots, transport, license,
pin, extraction and quarantine gates, then disables only its own new watches.
It does not start recurring polling or promote algorithms.

### Feed already acquired bytes without downloading again

`statewright_import_mathematical_acquisitions` verifies the completed pilot store,
its recorded event head, file-review pins, license provenance and preflight.
It copies only acquisition records and artifact bytes, then reruns source
assessment, extraction and brain-feed checks in the target knowledge context.
Pilot novelty conclusions and candidate records are not copied. Imported watches
remain disabled and no canonical algorithm is admitted. Native store locks are
nonblocking; an existing writer is never interrupted.

```sh
./build/statewright_import_mathematical_acquisitions \
  ./build/mathematics-pilot-store-v1 . ./resources \
  ./build/mathematics-pilot-evidence-v1 ./build/NEW_IMPORT_EVIDENCE
```

The importer is replay-safe with a new evidence directory; existing evidence is
never overwritten. Acquisition receipt costs remain historical, and the import
itself makes zero network requests. Existing target watches and canonical
algorithms are preserved.

On 2026-09-05 the user requested the import but explicitly chose to let the older
RFC/W3C batch finish first. `Tools/feed_acquired_mathematics_when_ready.sh` gates
the project import on that batch's completion, integrity and 40-watch cleanup
records. Exit 10 means still waiting. It runs one check, not a shell polling loop.
An app follow-up named "Feed reviewed mathematics after standards batch" checks
every ten minutes and stops after import completion. Its planned project evidence
directory is `build/mathematics-project-import-v1`; this is not complete merely
because the earlier isolated tests passed.

Offline import validation: 20/20 acquired sources were fed into a separate test
store with 20 quarantined candidates, zero enabled watches and zero admissions.
Replaying all 20 preserved the candidate count and passed integrity again. Evidence
is in `build/mathematics-import-test-evidence-v1` and
`build/mathematics-import-replay-evidence-v1`.

### Recorded pilot result (2026-09-05)

- Preflight: 20/20 eligible, including exact body-hash matches.
- Acquisition/feed: 20/20 successful; 20 complete fragments, zero truncations or
  extraction rejections, and zero exact/equivalent duplicate retrieval matches.
- Candidates: 20 quarantined with both `MATHEMATICAL_CONTEXT_REVIEW_REQUIRED`
  and `DOMAIN_BRANCH_AND_ERROR_BOUNDS_NOT_QUALIFIED`.
- Qualification-ready/accepted algorithms: zero. Cost per accepted algorithm is
  undefined, not zero. The separate numerical smoke checks are not qualifications
  of these acquired files.
- Recorded acquisition cost: 53,190 compressed / 192,614 decompressed bytes and
  2,160 ms fetch time. Preflight separately used the same byte totals and 7,776 ms
  fetch time. These timings exclude store processing and are not total CPU cost.
- Cleanup: all 20 pilot watches disabled. Integrity passed at event head
  `70bf9f205580333f784e1331cdc4c8421415fd0d13e413ecb947cbf3b59ca09d`.
- Pointwise numerical smoke: eight cases passed both backends in
  `build/mathematical-points-review-v1.json`; six verifier regression tests passed.
- Regression validation: all 18 CTest targets passed, including package tests,
  the existing acquisition workflows and the optional numerical verifier.
  Focused mathematical C++ checks passed 54 assertions across six test cases.

The project-store replacement feed remains separate from this isolated pilot.
The earlier standards feed was still running during this review; no competing
writer or recurring polling expansion was started.
