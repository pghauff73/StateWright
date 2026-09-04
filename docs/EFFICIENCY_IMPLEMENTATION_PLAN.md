# StateWright Efficiency Implementation Plan

**Status:** Proposed  
**Scope:** StateWright contracts, core, sources, providers, reasoning, SAA, and
EGCF production paths  
**Primary objective:** Remove history-size and catalog-size amplification from
normal operation without weakening deterministic identity, fail-closed
validation, immutable lineage, or durability guarantees.

## 1. Outcome

StateWright should retain its existing canonical outputs and governance
semantics while making common operations scale with the data they actually
touch rather than with the complete workspace history.

The completed work must provide:

- bounded-cost normal startup using an authenticated incremental checkpoint;
- one durable ledger synchronization per admitted record batch;
- incremental authoritative-cache maintenance without whole-cache sorting;
- indexed run, lease, receipt, event, and candidate queries;
- bounded canonical and reasoning search results, including exclusions;
- linear brain-feed dependency resolution;
- one indexed failure search per internet feed batch;
- lower allocation and exact-number copy overhead in mathematical kernels;
- reproducible performance benchmarks with enforced regression thresholds.

This plan does not authorize changes to canonical IDs, record schemas, policy
decisions, execution authority, or release qualification claims unless a phase
explicitly introduces a versioned additive migration.

## 2. Non-negotiable constraints

1. Existing canonical JSON, typed IDs, signatures, ordering, and deterministic
   tie-breaks remain byte-for-byte compatible.
2. The immutable object store and hash-linked event ledger remain
   authoritative. SQLite remains a rebuildable projection.
3. Corruption, ambiguous lease lineage, duplicate terminal receipts, and stale
   authority continue to fail closed.
4. A performance shortcut must not trust a cached value without authenticating
   the checkpoint and the suffix it covers.
5. Batching may reduce synchronization calls but must preserve the documented
   durable-prefix and replay behavior.
6. Internal hash tables may not leak nondeterministic iteration order into
   serialized output. Sort at canonical output boundaries.
7. All new persistent projection fields and indexes are additive and
   rebuildable from authoritative records.
8. Optimization work must be measured with release builds on a local durable
   filesystem; correctness tests alone are not performance evidence.

## 3. Baseline and measurement protocol

Before changing production code, add a dedicated performance executable or
CTest-labelled benchmark suite. Benchmarks must use deterministic generated
fixtures and report JSON so results can be compared in CI and release evidence.

Record at least median, p95, maximum, processed-item count, bytes read/written,
and peak resident memory where available. Run each short benchmark enough times
to make startup noise visible, but keep the complete local suite below five
minutes.

### Required baseline workloads

| Benchmark | Dataset sizes | Measurements |
| --- | --- | --- |
| EGCF open/read | 1k, 10k, 100k objects and events | open latency, bytes read, RSS |
| Record admission | batches of 1, 10, 100, 1k | records/s, sync calls, projection time |
| Projection rebuild | 1k, 10k, 100k records | wall time, peak RSS, database size |
| Run status | 1k, 10k, 100k orchestration records | worker-filtered latency, returned bytes |
| Lease/receipt lookup | 1k, 10k, 100k histories | lookup latency by action/job key |
| Canonical search | 1k, 10k, 100k algorithms | exact, semantic, lexical, no-match latency |
| In-memory SAA search | 1k, 10k, 100k algorithms | query latency, peak result memory |
| Brain feed | 64, 512, 4096 items | chain, fan-out, independent, cyclic graphs |
| Internet failure matching | 10, 100, 1k fragments x 1k, 10k failures | latency, query count |
| Exact kernels | published maximum dimensions and budgets | latency, allocations, RSS |
| Supervisor wake | 1k, 10k, 100k histories | status-to-action latency, total wake time |

### Initial performance gates

The baseline phase records current behavior rather than immediately failing CI.
After each optimized phase lands, introduce a threshold for that workload:

- no more than 15% median or 25% p95 regression relative to the accepted
  release baseline;
- no more than 10% peak-memory regression unless documented and approved;
- indexed point lookups must demonstrate sublinear scaling from 10k to 100k;
- result memory must be bounded by configured output limits rather than catalog
  size;
- correctness, sanitizer, package, and release tests must remain green.

## 4. Phase 1: Performance observability

### Work

1. Add a small instrumentation interface for counters and scoped durations that
   compiles to negligible overhead when disabled.
2. Instrument:
   - authoritative object and event reads;
   - JSON parses and canonical serializations;
   - SHA-256 bytes processed;
   - SQLite statements and rows visited;
   - ledger synchronization calls;
   - catalog candidates evaluated and excluded;
   - brain-feed dependency probes;
   - exact-kernel candidate and term budgets.
3. Add the deterministic benchmark workloads from section 3.
4. Add CTest labels `performance-smoke` and `performance-full`.
5. Record compiler, build type, filesystem, CPU, dependency identities, and
   dataset signature with every benchmark result.

### Acceptance criteria

- The smoke suite completes in under 60 seconds on the reference development
  host.
- Benchmark output is canonical JSON and includes a dataset signature.
- Instrumentation does not alter canonical domain objects or signatures.
- Repeated benchmark runs over the same fixtures select the same algorithms and
  produce the same functional outputs.

## 5. Phase 2: Incremental event and projection validation

### Problem

Normal EGCF construction currently validates the complete event ledger, reloads
all authoritative objects and events, and may validate the chain more than once.
Director reads can trigger another complete projection validation.

### Work

1. Introduce an authenticated projection checkpoint containing:
   - schema version;
   - authoritative object count and digest;
   - event count and event head;
   - ledger byte offset after the last complete event;
   - projection file identity;
   - checkpoint signature or digest over the complete checkpoint material.
2. On normal open:
   - validate checkpoint structure and identity;
   - verify the projection file stamp;
   - read and validate only the ledger suffix after the checkpoint offset;
   - discover only authoritative object files not represented by the checkpoint;
   - append the verified suffix to the in-memory state and projection.
3. Fall back to a complete authoritative replay if any checkpoint condition is
   missing, stale, inconsistent, or corrupt.
4. Retain explicit `validate_projection`, `verify_integrity`, and rebuild paths
   for complete validation.
5. Make validation state visible within `EgcfStore` so callers such as the
   Internet Director do not repeat a validation already performed by the same
   instance.
6. Avoid separately parsing the same event ledger for `events()` and
   `validate_chain()` during a single validation pass.

### Tests

- valid checkpoint with no suffix;
- valid checkpoint with one and many suffix events;
- truncated final ledger line;
- tampered suffix event and payload;
- tampered checkpoint fields;
- stale projection file stamp;
- missing or extra object files;
- complete replay produces the same checkpoint and query results;
- crash between authoritative append, projection append, and checkpoint update.

### Acceptance criteria

- Warm open with no changes performs `O(1)` metadata reads plus bounded SQLite
  checks and does not parse complete object or event history.
- Warm open latency grows by less than 2x between 10k and 100k unchanged
  records.
- Full integrity verification remains capable of detecting every corruption
  detected before this phase.

## 6. Phase 3: Durable batch admission and cache maintenance

### Work

1. Add `EventStore::append_batch`:
   - redact and validate every payload;
   - construct event IDs, payload hashes, and the complete hash-linked sequence;
   - append all canonical JSON lines through one descriptor;
   - perform one durable synchronization;
   - publish the new in-memory head only after successful synchronization.
2. Define and test behavior for partial writes and synchronization failure.
3. Use the batch API from `EgcfStore::register_records`.
4. Replace whole-vector sorting after each admission with:
   - sorted new records plus `std::ranges::merge`, or
   - keyed lookup storage with a sorted canonical view.
5. Batch object and projection preparation where possible while retaining
   immutable object-file identities.
6. Cache the event redaction context once per process:
   - configured regular expressions;
   - environment-derived secret values;
   - known non-secret token keys.

### Tests

- batch identities equal sequentially constructed event identities under fixed
  event stamps;
- one synchronization per non-empty batch;
- empty and duplicate-only batches do not synchronize;
- injected short write, append failure, and sync failure;
- replay after every injected batch failure point;
- cache ordering matches the existing canonical ordering exactly;
- secret redaction remains equivalent for nested payloads.

### Acceptance criteria

- A 1,000-record batch requires one ledger synchronization.
- Batch admission is at least 5x faster than the baseline 1,000-record workload
  on the reference durable filesystem.
- Incremental cache maintenance is `O(N + K log K)` or better for `K` new
  records, with no complete sort per individual record.

## 7. Phase 4: Indexed EGCF operational queries

### Work

1. Extend the rebuildable internet projection with typed columns needed by
   operational queries:
   - `worker_id`;
   - `run_id`;
   - `plan_id`;
   - `resume_of_run_id`;
   - `event_type`;
   - `action_key`;
   - `job_id`;
   - `lease_state`;
   - `predecessor_lease_id`;
   - `expires_at`;
   - receipt terminal/disposition fields.
2. Add compound indexes for actual access patterns, including:
   - `(object_type, worker_id)`;
   - `(object_type, run_id, event_type)`;
   - `(object_type, action_key)`;
   - `(object_type, job_id)`;
   - `(object_type, predecessor_lease_id)`;
   - `(object_type, plan_id)`.
3. Add typed query methods that return only matching records.
4. Rewrite:
   - latest fetch lease;
   - latest action lease;
   - terminal action receipt;
   - run terminal status;
   - worker-scoped nonterminal runs;
   - filtered run status;
   - action explanation.
5. Compute each optional lease or receipt once when constructing an explanation.
6. Confirm index use with `EXPLAIN QUERY PLAN` tests.

### Tests

- projection rebuild populates every new column from authoritative records;
- old workspaces rebuild without migration of authoritative objects;
- conflicting lease leaves are still detected;
- duplicate terminal receipts are still rejected;
- filtered results exactly match a reference full-scan implementation;
- query plans use the intended indexes at representative sizes.

### Acceptance criteria

- Point lookup and worker-filtered status do not deserialize unrelated records.
- Lookup latency demonstrates sublinear scaling between 10k and 100k records.
- `run-status` response size is proportional to selected runs and their related
  records, not complete orchestration history.

## 8. Phase 5: Canonical algorithm search redesign

### Work

1. Define a versioned search-result contract with separate limits for:
   - returned candidates;
   - detailed exclusions;
   - total scanned/evaluated candidates;
   - lexical result candidates.
2. Preserve current defaults for small catalogs while providing explicit
   truncation metadata and counts.
3. Normalize and project searchable fields at admission:
   - domain;
   - input/output shape;
   - mathematical, semantic, representative, and structural signatures;
   - semantic meanings;
   - source structural hashes;
   - demotion/preference state.
4. Add relational indexes and an FTS table for lexical domain and semantic
   meaning search.
5. Replace the per-algorithm `sources(canonical_id)` call with a join or bounded
   bulk query.
6. Push exact and conjunctive filters into SQL before scoring.
7. Maintain only the best `K` eligible results using a bounded heap, followed by
   the existing deterministic final ordering.
8. Return bounded representative exclusions and aggregate the remainder by
   reason.
9. Preserve an explicit exhaustive diagnostic mode for operators, with a hard
   caller-supplied bound.

### Compatibility

If exclusion truncation changes the serialized result, introduce a new search
protocol version rather than silently changing a signed v1 result. Continue to
support v1 only within a documented maximum catalog size or explicit exhaustive
bound.

### Acceptance criteria

- Exact signature searches use an index and avoid a complete catalog scan.
- A normal search over 100k algorithms returns bounded JSON and bounded memory.
- Selected IDs and ranking order match the reference implementation for the
  complete small-catalog fixture matrix.
- No N+1 source query remains.

## 9. Phase 6: In-memory registry and SAA search indexes

### Work

1. Add secondary indexes to `AlgorithmRegistry`:
   - exact algorithm ID to vector index;
   - command ID to algorithm indices;
   - algorithm ID/digest to qualification indices.
2. Return references or lightweight views inside selection paths instead of
   copying definitions and qualifications where ownership permits.
3. Extend `AlgorithmSearchIndex` with precomputed normalized metadata and
   posting lists for:
   - domain;
   - primitives;
   - invariants;
   - semantic terms;
   - structural hash;
   - qualification status.
4. Intersect the smallest posting lists first.
5. Bound detailed exclusions and expose aggregate exclusion counts.
6. Use internal hash maps where ordering is irrelevant; sort identities only
   when emitting canonical results.

### Acceptance criteria

- Exact registry resolution is average `O(1)`.
- Command selection evaluates only algorithms registered for that command.
- Search work is proportional to posting-list candidates for selective queries.
- Canonical ordering remains unchanged across repeated runs and insertion order.

## 10. Phase 7: Linear brain-feed processing

### Work

1. Replace vector-based resolved lookup with an `item_id` index.
2. Build a dependency graph once per batch:
   - indegree for each item;
   - reverse dependency list;
   - missing-reference diagnostics;
   - separate evidence and ordinary dependency annotations where required.
3. Process ready nodes with a deterministic priority queue or sorted ready set.
4. Detect unresolved cycles after the queue empties.
5. Preserve the original input ordering when constructing the final receipt.
6. Batch-register item and disposition records where lifecycle semantics permit.
7. Load historical dispositions once and build exact/content indexes once per
   feed call.

### Tests

- independent, chain, diamond, fan-in, fan-out, missing, and cyclic graphs;
- output equivalence with the current implementation;
- deterministic results under permuted input order where the contract permits;
- maximum 4,096-item chain and dense bounded dependency fixtures;
- duplicate exact and content signatures.

### Acceptance criteria

- Dependency resolution is `O(B + D)` apart from ordered-ready-set costs.
- A 4,096-item chain completes within an agreed release threshold and without
  quadratic dependency probes.
- Final receipt ordering and signatures remain compatible.

## 11. Phase 8: Indexed internet failure matching

### Work

1. Project normalized failure text and structured failure fields into SQLite
   FTS at registration/rebuild time.
2. Compute lexical terms once per fragment.
3. Query failures through one prepared statement per batch or a bounded query
   per distinct term set.
4. Deduplicate matched failure IDs and apply a configurable result limit.
5. Record total matches, returned matches, and truncation in a versioned
   retrieval receipt if required.
6. Eliminate repeated payload serialization and lowercasing during feed
   processing.

### Acceptance criteria

- Failure payloads are not listed and decoded once per fragment.
- Query count is bounded independently of total failure history.
- Matching results agree with the reference substring implementation for the
  compatibility fixture corpus.

## 12. Phase 9: Exact-kernel and allocation cleanup

This phase follows the store and search work because micro-optimizations will
not compensate for whole-history amplification.

### Work

1. Apply reviewed performance-analysis findings:
   - pass read-only `mpq_class`, `mpz_class`, matrices, vectors, JSON, and large
     strings by `const&`;
   - retain pass-by-value where the function consumes or canonicalizes input;
   - add capacity reservations where an output bound is known;
   - remove ineffective `std::move` operations on const/reference values;
   - avoid repeated normalization and signature generation inside inner loops.
2. Precompute adjacency lists for reasoning color refinement rather than
   scanning all edges for every node and iteration.
3. For probation duplicate detection, use a hash set internally and sort
   signatures once for canonical output.
4. Add budget telemetry for:
   - reasoning canonical permutations;
   - MIMO port permutations;
   - representative rank terms;
   - polynomial terms and coefficient bits;
   - nonlinear search candidates;
   - Lie/frontier expansion.
5. Add pre-operation size/bit checks where an exact-arithmetic intermediate can
   grow substantially before the existing post-operation bound is checked.

### Acceptance criteria

- Performance-oriented static analysis has no unreviewed expensive-copy
  warnings in production code.
- Maximum-bound kernel benchmarks do not regress correctness or exceed agreed
  memory ceilings.
- Canonical signatures remain identical to the pre-optimization fixtures.

## 13. Phase 10: CI and release integration

### Work

1. Add CI jobs for:
   - developer correctness;
   - sanitizer correctness;
   - release build and package smoke;
   - performance smoke on every change;
   - full performance suite on a stable scheduled runner.
2. Store benchmark JSON as build artifacts.
3. Compare results only between compatible machine/build identities.
4. Add accepted performance baselines to release inputs with explicit review.
5. Extend release evidence with:
   - performance dataset signatures;
   - benchmark result hashes;
   - threshold results;
   - known measurement limitations.
6. Update `docs/RESIDUAL_RISKS.md` only after each risk is actually reduced and
   verified.

### Acceptance criteria

- Performance regressions block integration according to the adopted
  thresholds.
- Release evidence distinguishes functional qualification from performance
  qualification.
- No benchmark result grants execution or release authority.

## 14. Work breakdown and dependency order

| Order | Work package | Depends on | Primary files/modules |
| --- | --- | --- | --- |
| 1 | PERF-001 Baseline harness | None | `Tests/`, CMake presets |
| 2 | PERF-002 Instrumentation | PERF-001 | contracts, core, EGCF, SAA |
| 3 | PERF-003 Incremental checkpoints | PERF-001/002 | `core/event_store`, `egcf/store` |
| 4 | PERF-004 Batch ledger append | PERF-002 | `core/event_store`, `egcf/store` |
| 5 | PERF-005 Incremental cache merge | PERF-002 | `egcf/store` |
| 6 | PERF-006 Operational projection indexes | PERF-003 | `egcf/store`, internet stores |
| 7 | PERF-007 Indexed run/lease APIs | PERF-006 | internet store/orchestrator/director |
| 8 | PERF-008 Canonical search v2 | PERF-002/006 | canonical algorithm store |
| 9 | PERF-009 Registry indexes | PERF-002 | registry and selection engine |
| 10 | PERF-010 SAA posting lists | PERF-002 | SAA search and reasoning fit |
| 11 | PERF-011 Linear brain feed | PERF-004 | brain feed |
| 12 | PERF-012 Failure FTS | PERF-006/011 | internet feed, EGCF projection |
| 13 | PERF-013 Exact-kernel cleanup | PERF-001/002 | reasoning and SAA mathematics |
| 14 | PERF-014 CI performance gates | All measured phases | CI and release tooling |

PERF-003, PERF-004, and PERF-005 should be developed as separately reviewable
changes even if they are released together. PERF-008 requires an explicit
protocol compatibility decision before implementation.

## 15. Review and rollout strategy

Each work package should follow this sequence:

1. Land the deterministic benchmark fixture and capture the unoptimized result.
2. Add characterization tests for current canonical outputs and failure modes.
3. Implement one optimization without unrelated refactoring.
4. Run developer, sanitizer, release, package, and focused fault tests.
5. Compare functional identities and performance measurements.
6. Test upgrade/rebuild behavior using a copied pre-change workspace.
7. Document the accepted performance result and residual limitation.

Roll out projection changes behind automatic rebuild support. Roll out result
contract changes under explicit protocol versions. Do not delete old
projections or evidence during migration; projections are replaceable, but
authoritative objects and ledgers are not.

## 16. Definition of done

This plan is complete when:

- normal unchanged-workspace startup is checkpoint-based and bounded;
- full validation is explicit rather than repeated within every operation;
- batch record admission uses one ledger synchronization and incremental cache
  maintenance;
- all common orchestration point queries use projection indexes;
- canonical and in-memory searches have bounded work/output modes;
- brain-feed dependency processing is linear in items plus dependencies;
- internet failure matching uses indexed batch retrieval;
- exact kernels expose and respect measured resource budgets;
- the performance suite covers all published maximum algorithm dimensions;
- CI enforces accepted functional and performance gates;
- developer, sanitizer, release, package, migration, crash-recovery, and release
  evidence tests pass;
- canonical identities and authority boundaries remain unchanged except where a
  separately reviewed versioned protocol explicitly records a change.

## 17. Expected result

The first five implementation phases should deliver the largest improvement:
lower supervisor latency, much faster bulk admission, and stable operational
queries as history grows. Search and brain-feed phases then remove catalog and
batch-size cliffs. Exact-kernel cleanup should provide smaller but measurable
CPU and memory improvements while preserving the carefully bounded mathematical
design.

The target is not merely a faster fixture. It is a system whose routine cost is
proportional to the current operation, with complete-history work reserved for
explicit integrity and release qualification procedures.
