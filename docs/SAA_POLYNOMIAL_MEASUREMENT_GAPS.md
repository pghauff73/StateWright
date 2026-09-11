# Polynomial measurement evidence: implementation boundary

## Implemented

Exact-rational Horner execution, a direct-power-sum timing comparator, immutable
protocol and workload freezes, start receipts, raw results, descriptive timing
summaries and provenance-chain validation are implemented. The existing internet
orchestrator accepts explicitly queued frozen measurement designs through
`COLLECT_POLYNOMIAL_MEASUREMENT`, using native budgets, leases and receipts.
Interrupted work with a persisted result can be reconciled without retiming.

These capabilities do not establish source truth, independent review or final
qualification. Test candidates, simulated policy inputs and synthetic lifecycle
observations must not be exported as production qualification evidence. Actual
timings collected for a synthetic candidate are timings of that fixture only.

## Benchmark evidence is not runtime alone

`Core/include/statewright/saa/oiec_bench_gate.hpp` requires every track:

- TRUTHGROUND
- MEANINGPATH
- SEMANTICREP
- MEANINGGROUND
- WORKGROUND
- PROGRESSCERT
- AGENTWORK

The gate also requires successful, non-simulated registered evidence, coverage of
the corresponding requirements, sufficient independence groups and review.
Repeated timing pairs do not establish these track scores. Do not assign perfect
scores based on polynomial execution success or rename a timing ratio as a track.

The reasoning benchmark in `Core/include/statewright/reasoning/benchmark.hpp`
uses eight different task categories. Its outputs cannot be relabeled as these
seven tracks without a justified, frozen scoring specification and appropriate
workloads. The inspected interfaces do not supply such a mapping.

The polynomial coverage diagnostic reports missing or out-of-range scores and
missing integrity snapshots. Structurally present values remain unverified;
they still go through the existing measurement, review and qualification gates.

## Longitudinal integrity is a separate collection problem

Integrity snapshots require actual generations, knowledge counts, contradictions,
drift, false admissions, correction opportunities and recurrences, retrieval
queries and correct selections, and equivalent-failure opportunities and retries.
Do not derive these counts from arithmetic trials or invent operational history.
The legacy integrity rate helper supplies defaults for zero denominators; those
defaults are not evidence that the corresponding behaviour was observed.

## Remaining implementation and evidence

1. Freeze justified track-specific workloads, expected results, scoring rules and
   independent method provenance before collecting their outcomes.
2. Collect actual track results and operational integrity history, preserving
   failed outcomes and distinguishing absent observations from zero failures.
3. Bind the resulting measurement summary to its protocol freeze and native raw
   evidence. Descriptive performance statistics remain separate from gate scores.
4. Obtain the required independently grounded reviews; do not generate production
   reviewer identities or keys to bypass the configured trust policy.
5. Complete source-bound translation and remaining canonical retrieval consumers
   before claiming end-to-end support for an ingested polynomial candidate.
6. Qualify and observe a real candidate through probation. Reassess a higher-level
   candidate only after its actual dependencies and composed-error requirements
   are satisfied.

No live schedule configuration, source permissions or real candidate approval
was changed by the measurement implementation tests.
