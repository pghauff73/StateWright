# Polynomial source and independent evidence route

Status: research and implementation handoff, not an SAA evidence receipt or source permission.
Research date: 2026-09-08.

## Proven implementation boundary

The disposable CLI smoke test now stages a synthetic quadratic through native
fetching, extraction and source-bound translation. It freezes and registers a
polynomial protocol, freezes its workload, collects actual paired timings, reuses
completed measurements on replay, and validates the stored measurement chain.
The candidate remains VALIDATION_READY without qualification or probation.

The successful local source is a deliberately labelled test procedure. It is not
a downloaded mathematical reference. Finding an equivalent equation online does
not retroactively give that fixture internet-source provenance.

## Mathematical context located

[DLMF 18.9](https://dlmf.nist.gov/18.9) supplies a recurrence, initial values and
the first-kind Chebyshev coefficient row. Substitution at degree two gives
`T_2(x) = 2*x^2 - 1`. This is a derivation from the reference, not an executed
experiment. [DLMF 18.5](https://dlmf.nist.gov/18.5) supplies another mathematical
representation. These references can guide a separately checked specification;
two pages from the same reference are not two independent experiment groups.

The [DLMF-specific notice](https://dlmf.nist.gov/about/notices) restricts bulk
copying and commercial reuse. Do not classify these pages as automatically
permitted just because they are hosted by NIST. No source-policy exception or
automated ingestion permission was registered during this research.

## Next source-resolution target

### Existing-store source takes priority

A subsequent read-only inspection found an already downloaded Fungrim Chebyshev
source in the intended recurring workspace. This is now the preferred immediate
source-adapter target; SymPy remains supplementary research, not a newly enabled
watch. The watch listing contains historical versions as well as successors, so
its 365 records must not be reported as 365 currently enabled unique sources.

The pinned [Fungrim source](https://raw.githubusercontent.com/fredrik-johansson/fungrim/b7c3ca6e565e1058638cab6ba2bc811090296767/pygrim/formulas/chebyshev.py)
contains first-kind table entry `85e42e`, including degree two, explicit variable
and complex-domain assumptions. Table rows cover degrees 0-15, not degree 16.
A rational-only implementation on [-1,1] would restrict that mathematical domain.
Its declared resource limits would still be implementation restrictions, not
statements copied from the source.

Authoritative records returned by the native store:

- Workspace: `/home/pamela/Projects/StateWright/build/saa-feed-100-20260905-EqQArB/store`.
- Snapshot: `internet-source-snapshot:sha256:7974346715cb3806b29271c978ca4a4c39c7abb99dcae57c8b210bd2a9ae34d8`.
- Recorded body SHA-256: `8f4b0df5f636d89b0ddcbdf799d68f4197eb60a8d0296b41fac373980253377e`.
- Recorded body size: 24613 bytes.
- Policy assessment: `internet-policy-assessment:sha256:08137d68ac7fd7e86de3393fada95ca3b498b4169833cc6aa47f7d57e36e6f86`.
- Fetch receipt: `internet-fetch-receipt:sha256:6e91f40ccc74ebe505536fbcca1e14f245a169e6b571828e16e8337fbc3e5985`.
- The returned assessment reports SOURCE_ADMISSIBLE, MIT, robots allowed, and
  no blocking reasons. This is a stored assessment, not a new license review or
  renewed permission to fetch the live site.

Read-only query outputs are in
`/tmp/statewright-polynomial-chebyshev-snapshots.json` and
`/tmp/statewright-polynomial-chebyshev-policy.json`.

Next: inspect the stored extraction boundary for entry `85e42e`, preserve its
table relation and assumptions, and implement a bounded non-executing adapter
for that format. Preserve existing quarantine history. Source admissibility
does not establish translation, independent correctness, review or acceptance.

### Supplementary SymPy source

[SymPy's Chebyshev documentation](https://docs.sympy.org/latest/modules/functions/special.html#sympy.functions.special.polynomials.chebyshevt)
contains the degree-two example and mathematical context, with an implementation
link. The [project](https://github.com/sympy/sympy) identifies its general license
as New BSD, subject to exceptions in its [license file](https://github.com/sympy/sympy/blob/master/LICENSE).
This makes it a promising target for exact-file license assessment, not an
already admissible SAA source.

Required bounded follow-up:

1. Resolve the documentation's implementation link to an immutable revision.
2. Assess the exact source and applicable license at that revision, including
   any dependency-specific exceptions. Retain attribution requirements.
3. Retrieve only the required documents through the existing native acquisition
   and source-policy path. Do not run the downloaded Python code.
4. Preserve the equation, symbol definitions, selected degree and dependency
   relationships in a source bundle. Record rational-only inputs, [-1,1] and
   resource limits as the implementation's restricted contract, not invented
   quotations from the source.
5. Add an explicit source-format adapter if needed. The current labelled-text
   translator is not evidence that arbitrary documentation examples translate.
   Rewriting a page into fixture syntax must not masquerade as original text.

### Resolved source link and bounded inspection

The documentation's Chebyshev source link resolves to
[polynomials.py at 16fa855354eb7bcabd3fe10993841e03b1382692](https://github.com/sympy/sympy/blob/16fa855354eb7bcabd3fe10993841e03b1382692/sympy/functions/special/polynomials.py).
This fixes the intended revision instead of relying on a moving branch.

The initial source-link request returned HTTP 429 (Too Many Requests). A later
bounded retry succeeded through the same browser source path. The source and
same-revision license were then inspected as rendered pages. This clears the
browser inspection blocker, not the outstanding native acquisition requirement.
No downloaded code was executed and no native receipt or byte hash was fabricated.

The pinned `chebyshevt` docstring contains the degree-two expression and
orthogonality interval. Its fixed-degree implementation delegates through
`chebyshevt_poly`; the base helper constructs a polynomial and substitutes the
input. Translating that machinery is different from translating the documented
fixed formula.

The same-revision
[orthopolys.py](https://github.com/sympy/sympy/blob/16fa855354eb7bcabd3fe10993841e03b1382692/sympy/polys/orthopolys.py)
selects a recurrence implementation for positive degrees below 64, covering the
proposed fixed degrees 1-16. That path depends on polynomial shifting, scalar
multiplication, subtraction and coefficient-domain operations. Its initial
polynomials and recurrence are explicit. The larger-degree product algorithm
is outside the proposed scope. These are immediate dependencies, not a claim
that the entire implementation closure has been reviewed.

The pinned
[license](https://github.com/sympy/sympy/blob/16fa855354eb7bcabd3fe10993841e03b1382692/LICENSE)
contains redistribution conditions requiring preserved notices and disclaimers,
and prohibiting unauthorized endorsement. It also lists additional component
notices. Exact-file attribution and the native policy decision remain separate
work; do not register a blanket license approval for uninspected dependencies.

Resumable research state:

- Target operation: first-kind Chebyshev polynomial at fixed degree two.
- Source link: resolved to the revision above.
- Source bytes and content hash: pending bounded native acquisition.
- Same-revision license: inspected; exact-file attribution and native assessment
  remain pending.
- Implementation dependency closure: immediate recurrence operations identified;
  transitive helper and coefficient-domain implementations remain unassessed.
- Existing source-policy permission and robots assessment: not established by
  this browser research.
- Independent oracle lineage: undecided; the documentation and its linked
  implementation are one lineage, not independent witnesses.

Next implementation decision: use a narrowly scoped, source-bound documentation
formula adapter for fixed degree two, rather than claiming to translate the
whole SymPy generator. Preserve the original docstring as source text and record
any syntax normalization and contract restriction in derived provenance.
The independent oracle must not simply call the same generator whose output
supplied the candidate coefficients. Native acquisition and independent evidence
remain blockers to candidate qualification, not mathematical unsuitability.

## Independent correctness experiment still required

Choose the implementation and oracle lineages before producing qualification
results. If the candidate comes from SymPy, SymPy-generated expected values
alone are not independent evidence. Horner and direct power-sum evaluation also
share coefficients, GMP, compiler, host and validation infrastructure here.

A possible additional method is an exact recurrence evaluator whose constants
come from an independently assessed specification, rather than the candidate's
coefficient array. It needs its own bounded implementation, source bindings,
negative controls and review. Mathematical agreement between representations
does not by itself establish implementation independence.

Freeze input partitions, expected-result derivations, oracle artifacts,
thresholds and workload before evaluation. Keep development cases separate from
held-out cases. Labels and different random seeds do not establish independent
experimental lineage.

## Remaining acceptance requirements

- Independently justified expected results and authenticated review evidence.
- A defensible equivalent performance baseline. Catalogue absence establishes
  local capability absence, not a timing baseline or superiority.
- Genuine measurements for the required benchmark tracks, with justified
  scoring rules. Raw elapsed time is not a substitute for seven track scores.
- Actual longitudinal integrity and probation observations, not fabricated
  counts, backdated fixtures or repetitions of arithmetic cases.
- Native recurring integration with the actual approved source and frozen
  designs. Existing test queues do not demonstrate a live candidate transition.

No higher-level Airy candidate was reassessed: its real dependencies and composed
error requirements have not been shown to be satisfied by this polynomial work.

## Current test evidence

- `/tmp/statewright-polynomial-cli-tests.log`: 112 internet tests, 1433 assertions.
- `/tmp/statewright-polynomial-chain-cli-build.log`: command-interface build.
- `/tmp/statewright-polynomial-chain-cli-tests.log`: expanded CLI smoke test.
- `Tests/internet_cli_smoke.sh`: synthetic lifecycle, polynomial source and
  measurement replay, malformed requests, chain rejection and no-admission checks.

These are local implementation test results. They are not portable qualification
receipts, evidence of independent reviewers, or approved internet candidates.
