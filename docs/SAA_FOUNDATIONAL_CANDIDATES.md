# Foundational candidates: implementation increment 1

## Implemented scope

The offline catalogue and planner identify existing structural primitives,
missing qualification adapters, proposed foundations, and unresolved higher-level
dependencies. They do not write to EGCF, register watches, retrieve linked code,
translate candidates, or emit qualification evidence.

The initial roots are the observed top-level Boost real Airy Ai implementation
and a proposed exact-rational polynomial evaluator. Bessel internals remain
explicitly unresolved rather than replaced with an invented dependency closure.
Source URLs are locators, not reviewed snapshots. Every source still requires
native policy assessment, immutable capture and mathematical review.

## Inventory finding and stop condition

`Core/src/saa/algorithm_ir.cpp` registers arithmetic, predicate and control
primitives including ABS, ADD, MULTIPLY, DIVIDE, MIN, MAX, SELECT and ITERATE.
Membership in that structural registry is not proof of runtime execution support.

`Core/src/egcf/internet_experiment.cpp` accepts only one scalar input/output,
at most two nodes, and the IDENTITY, CONST or exact rational a*x+b shapes.
It does not evaluate general polynomial DAGs, predicates or numerical kernels.
The existing mathematical translation quarantine remains unchanged.

Consequently this increment stops before translation and experiments, as the
implementation plan requires when unsupported runtime integration is identified.
No foundational candidate has been registered, translated or accepted by this work.

## Offline use

From the project root, use a new output path:

```sh
python3 tools/saa_foundations.py --output build/foundation-dependency-plan.json
python3 tools/saa_foundations.py --output build/foundation-dependency-plan.json --resume
```

Each invocation materializes at most eight new operation records by default.
`--max-nodes` changes that budget. `--max-depth` limits dependency traversal;
increase it when the report contains depth-deferred operations. The input
catalogue is limited to 128 operations, 32 dependencies per operation and 1 MiB.
Cycles and undeclared dependencies fail before an output update. Resume binds
to hashes of the catalogue and proposed contract plus root IDs and planner
version. Changed inputs require a new output path. The report is atomically
replaced; it is a mutable planning checkpoint, not an immutable evidence receipt.
Use only one offline planner writer per output path.

`CATALOGUE_TRAVERSAL_COMPLETE` means only that the selected catalogue graph has
been traversed. Mathematical context stays INCOMPLETE and acceptance stays NONE.
Saved progress is not a trusted source of graph definitions or native evidence.

## First foundation contract

`resources/saa/contracts/polynomial-exact-rational-v1.json` specifies a proposed
degree-0..16 fixed-coefficient Horner evaluator over rational inputs in [-1,1].
The bounded input sizes, intermediate-size ceiling and failure behaviour are
requirements for the future adapter, not currently enforced runtime features.
Exact evaluation of a polynomial does not certify that polynomial as an Airy
approximation. No performance improvement or implementation independence is
claimed. A direct-form oracle sharing GMP must disclose that shared dependency.

## Required next increments

1. Capture and review precise supporting sections; bind URLs, hashes and native
   receipts to each contract and dependency. Resolve conflicts before proceeding.
2. Extend the native experiment representation beyond slope/bias to bounded
   exact-rational DAG execution. Preserve existing affine protocol semantics.
3. Add a source-bound polynomial translator with degree, bit-size and domain
   checks. Bind coefficient hashes and evaluation order to candidate identity.
4. Register a separate protocol with frozen held-out groups, independently
   justified expected values, an equivalent baseline and raw measurements.
5. Add machine review and existing lifecycle integration without manufacturing
   reviews, scores, observation windows or independence claims.
6. Integrate native durable planning receipts into the existing rotation only
   after their producer and consumer APIs exist. Never call the offline planner
   an admission step or silently broaden the 100-URL source registry.

## Planned validation, not executed

Validate cycle rejection, unknown dependencies, bounded continuation, changed
input rejection and shared-dependency deduplication. For the future translator,
exercise degree limits, invalid rationals, resource exhaustion and source
mismatches. Qualification checks must reject deliberately faulty polynomials,
missing measurements, shared-oracle mislabelling and stale artifact evidence.
No tests or build validation were run for this increment.
