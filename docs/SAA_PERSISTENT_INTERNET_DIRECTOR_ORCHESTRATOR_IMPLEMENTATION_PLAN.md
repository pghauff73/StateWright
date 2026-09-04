# SAA Persistent Internet Improvement Director and Orchestrator Implementation Plan

**Plan date:** 2026-09-04

**Target:** StateWright C++20

**Source baseline:** `857639cf80c7cb12fbcbefd751328df2cb887a03`

**Status:** Implemented and qualified on the current worktree on 2026-09-04;
release qualification is tracked in `docs/RELEASE_QUALIFICATION.md`

**Primary compatibility surface:** `internet-improvement`

**Promotion authority:** Deterministic autonomous policy; no per-candidate human
approval

## 1. Executive Decision

StateWright will add a persistent SAA internet-improvement Director and
Orchestrator as a minimal unified refactor over the implemented internet
acquisition, EGCF persistence, searchable algorithm algebra, OIEC-SR reasoning,
experiment qualification, autonomous promotion, and probation machinery.

The Director will be a deterministic, read-only policy engine. It will inspect
authoritative immutable records and produce a signed, bounded plan describing
which lifecycle action is eligible next and why.

The Orchestrator will be a restart-safe executor. It will lease one directed
action at a time, revalidate its preconditions, call the existing canonical
coordinator, persist exact result receipts, and ask the Director to re-plan from
the new authoritative state.

The implementation will not add a second database, a second algorithm catalog,
an internet-specific promotion authority, an unbounded crawler, a downloaded
code executor, or a model-controlled autonomous agent.

## 2. Source-Bound Baseline

This plan is grounded in the StateWright `main` branch at commit
`857639cf80c7cb12fbcbefd751328df2cb887a03`.

The baseline already provides:

- immutable EGCF objects, artifacts, events, supersedence, search, and rebuildable
  SQLite projections;
- persistent source policies, watches, fetch jobs, append-only fetch leases,
  receipts, snapshots, assessments, extraction receipts, and fragments;
- deterministic watch scheduling, due-job selection, expiry recovery, clock
  diagnostics, response-byte budgets, CPU budgets, and source-group ceilings;
- BrainFeed ingestion and existing-knowledge-first SAA retrieval;
- internet algorithm candidate records and immutable candidate supersedence;
- OIEC-SR advisory hypotheses, falsifiers, missing-evidence records, and
  deterministic fallback;
- exact internal algorithm experiments, repeated evidence aggregation,
  benchmark gates, integrity trajectories, and improvement opportunities;
- deterministic autonomous promotion policies with no SAA approval record;
- probationary canonical admission, bounded canary selection, observation,
  automatic promotion, automatic demotion, and previous-preference restoration;
- CLI actions for manual execution of every implemented lifecycle boundary;
- fixture, sanitizer, package, and release-evidence coverage for the current
  manual lifecycle.

## 3. Problem Statement

The implemented lifecycle is complete as a set of explicit operations, but it
does not yet have one durable component that coordinates the whole lifecycle.

The current `internet-improvement advance` action selects the next candidate
operation using a status switch in `Apps/statewright/cli.cpp`. It does not:

- schedule or execute the complete watch-to-candidate acquisition path;
- persist why a next action was selected;
- coordinate fetch freshness, retries, candidate validation, improvement
  opportunities, and probation observations under one bounded budget;
- prevent repeated reasoning, experiment, or blocked policy assessment for
  unchanged inputs;
- record orchestration leases, attempts, reconciliation, or terminal receipts;
- resume safely after a crash between durable lifecycle boundaries;
- distinguish unavailable experiment evidence from an eligible experiment;
- distinguish a real probation observation from an observation that has not yet
  been supplied;
- provide one reusable core routing service for the CLI and future timed runs.

The Director and Orchestrator will close this coordination gap without replacing
the canonical domain services that already own each lifecycle transition.

## 4. Objectives

The implementation must:

1. move lifecycle selection out of the CLI and into a deterministic core
   Director;
2. create stable signed plans with explicit eligibility, deferral, and blocking
   reasons;
3. execute only closed, typed internal lifecycle actions;
4. persist every plan, run, lease, attempt, and result in the existing EGCF
   object store;
5. resume or reconcile safely after process interruption;
6. preserve immutable candidate lineage and active-generation checks;
7. use registered experiment protocols rather than inventing expected results;
8. consume provenance-bound probation observations rather than fabricating
   correctness evidence;
9. keep OIEC-SR and external models advisory;
10. preserve approval-free SAA promotion and the separate EGCF command-execution
    authority boundary;
11. retain the existing manual CLI operations and `advance` compatibility;
12. provide a bounded `run-once` process suitable for a system timer;
13. qualify deterministic planning, idempotency, lease recovery, stale-state
    rejection, security boundaries, packaging, and release evidence.

## 5. Non-Negotiable Invariants

1. **One canonical algorithm owner.** `CanonicalAlgorithmStore` remains the sole
   owner of canonical mathematical algorithm identity and preferred retrieval
   state.
2. **One internet persistence authority.** New internet lifecycle facts are
   ordinary typed EGCF records registered through `InternetImprovementStore` and
   `EgcfStore`.
3. **Director purity.** Planning performs no network I/O, provider invocation,
   filesystem mutation outside normal record reads, or EGCF writes.
4. **Closed action algebra.** The Orchestrator executes only enumerated internal
   lifecycle actions. It cannot accept a shell command, arbitrary executable,
   script body, downloaded program, or free-form tool request.
5. **Internet content is hostile data.** Downloaded text, markup, source code,
   metadata, binaries, and embedded instructions never become policy or
   execution authority.
6. **Models remain advisory.** Provider output can propose interpretations,
   hypotheses, contradictions, falsifiers, and missing evidence. It cannot
   satisfy deterministic promotion, experiment, integrity, or probation gates.
7. **No SAA approval object.** Normal internet candidate promotion remains
   deterministic and approval-free.
8. **No authority expansion.** Canonical SAA admission does not grant EGCF C3 or
   C5 command authority and does not approve production ownership cutover.
9. **Immutable evidence.** Every action binds exact input IDs, output IDs,
   policies, protocols, provider identities, and signatures.
10. **Receding-horizon planning.** Candidate-changing actions cause a fresh plan
    from the newly active lineage before another action is executed for that
    candidate.
11. **No invented experiments.** Qualification is blocked without a registered,
    applicable experiment protocol containing trusted expected outputs.
12. **No invented observations.** Probation assessment is blocked until a real,
    provenance-bound observation input exists.
13. **At-least-once external work.** The design does not claim exactly-once HTTP
    requests across process failure.
14. **Exactly-once durable finalization.** Only the latest valid action lease may
    register a terminal action receipt for an action identity.
15. **Derived projections.** SQLite rows, run summaries, indexes, and dashboards
    remain rebuildable from immutable records.
16. **Deterministic clocks in tests.** Wall-clock due decisions and monotonic
    timeout behavior are injectable and reproducible.
17. **Compatibility before removal.** Existing command contracts remain valid;
    compatibility aliases are retained until a separately documented breaking
    release.

## 6. Canonical Ownership Matrix

| Semantic fact | Canonical owner | Director/Orchestrator use |
| --- | --- | --- |
| EGCF objects, artifacts, events, supersedence | `EgcfStore` | Read state and register typed records only |
| Internet source lifecycle records | `InternetImprovementStore` | Extend with orchestration record methods |
| Watch and fetch-job ordering | `statewright::sources` scheduler | Director composes existing pure scheduling functions |
| Improvement opportunity scoring | `statewright::saa` improvement scheduling | Director composes existing opportunity selection |
| Source capture and extraction | Existing source functions plus new thin source coordinator | Orchestrator delegates exact actions |
| BrainFeed and SAA retrieval | `InternetFeedCoordinator` | Orchestrator calls `process` |
| OIEC-SR reasoning | `InternetReasoningCoordinator` | Orchestrator invokes at most as directed |
| Experiment qualification | `InternetExperimentCoordinator` | Orchestrator supplies registered protocol inputs |
| Promotion predicates | `AutonomousPromotionController` | Orchestrator cannot bypass or reimplement predicates |
| Probation and demotion | `InternetProbationController` | Orchestrator consumes real observations |
| Canonical SAA identity | `CanonicalAlgorithmStore` | Director reads active facts; Orchestrator never writes a parallel catalog |
| Knowledge integrity and schedules | `KnowledgeGovernanceStore` | Director uses registered opportunities and integrity state |
| Next-action decision | `InternetImprovementDirector` | New canonical owner of orchestration decisions |
| Attempts, leases, and execution receipts | `InternetImprovementOrchestrator` records | New canonical owner of orchestration history only |

## 7. Architecture

### 7.1 Director

Add:

- `Core/include/statewright/egcf/internet_improvement_director.hpp`
- `Core/src/egcf/internet_improvement_director.cpp`

The Director exposes a pure planning operation:

```cpp
class InternetImprovementDirector final {
public:
  [[nodiscard]] InternetImprovementPlan plan(
      const InternetImprovementState &state,
      const InternetDirectorPolicy &policy) const;
};
```

The Director receives a normalized state snapshot rather than direct provider or
network access. A store-backed reader constructs that state before invoking the
pure planner.

### 7.2 State Reader

Add a narrow `InternetImprovementStateReader` in the Director implementation or
as a private implementation class. It may read:

- active watch IDs and watch generations;
- active fetch jobs and latest lease per job;
- source snapshots, assessments, extraction receipts, and fragments;
- active internet algorithm candidate IDs after supersedence;
- reasoning analyses and candidate reasoning lineage;
- experiment protocols and qualifications;
- promotion policies and assessments;
- probation admissions, observations, promotion decisions, and demotion
  decisions;
- knowledge-integrity records, improvement opportunities, and schedules;
- projection checkpoint and event head.

It must not reconstruct active state by selecting the newest filename or relying
on wall-clock ordering. It uses typed identity, supersedence, generation, and
latest-lease rules.

### 7.3 Orchestrator

Add:

- `Core/include/statewright/egcf/internet_improvement_orchestrator.hpp`
- `Core/src/egcf/internet_improvement_orchestrator.cpp`

The initial interface is:

```cpp
class InternetImprovementOrchestrator final {
public:
  [[nodiscard]] InternetOrchestrationResult run_once(
      InternetOrchestrationRequest request);

  [[nodiscard]] InternetOrchestrationResult resume(
      std::string_view run_id,
      InternetOrchestrationRequest request);
};
```

`run_once` executes a bounded number of actions or stops at its time, response
byte, CPU, provider-call, or risk budget. The default is one action.

The Orchestrator does not own lifecycle policy. For each iteration it:

1. reads authoritative state;
2. asks the Director for a plan;
3. registers the exact plan and run identity;
4. selects one eligible action using deterministic ordering;
5. acquires an append-only action lease;
6. revalidates the action preconditions;
7. calls the canonical stage implementation;
8. persists a terminal action receipt;
9. closes or abandons the lease;
10. re-plans if the run budget permits another action.

### 7.4 Source Coordinator Extraction

The fetch, source-assessment, and extraction behavior currently assembled in the
CLI must move to a thin core coordinator before orchestration.

Add:

- `Core/include/statewright/egcf/internet_source_coordinator.hpp`
- `Core/src/egcf/internet_source_coordinator.cpp`

It owns no new source semantics. It packages the existing source policy, HTTP
provider, capture, assessment, snapshot-byte, and extraction calls into methods
that both the CLI and Orchestrator use.

The CLI must delegate to this coordinator so there is one implementation of
lease validation, bounded fetch, capture, failure receipt, source assessment,
and deterministic extraction.

## 8. Director Data Model

### 8.1 Action Kinds

Define a closed enum:

```cpp
enum class InternetDirectedActionKind {
  recover_expired_lease,
  schedule_fetch,
  execute_fetch,
  assess_source,
  extract_snapshot,
  feed_extraction,
  reason_candidate,
  qualify_candidate,
  assess_promotion,
  admit_probation,
  select_probation_candidate,
  consume_probation_observation,
  revalidate_source,
  verify_integrity
};
```

Do not add a generic action string that can be interpreted as a command.

### 8.2 Directed Action

`InternetDirectedAction` contains:

- schema and action-algebra versions;
- action kind;
- subject ID and subject type;
- active-generation or expected-status precondition;
- input record IDs;
- policy and protocol IDs;
- dependency action IDs;
- not-before and deadline timestamps;
- deterministic priority, cost, risk, response-byte, and CPU estimates;
- retry class and retry ceiling;
- blocked or deferred reasons;
- stable action key and action signature.

The action key is derived from semantic inputs:

```text
SHA-256(action kind,
        subject identity,
        active generation or candidate signature,
        relevant source/policy/protocol identities,
        action algebra version)
```

Worker identity, acquisition time, and attempt number are not part of the action
key. They belong to leases and attempts.

### 8.3 Director Policy

`InternetDirectorPolicy` contains:

- source scheduler limits;
- improvement scheduling policy;
- maximum actions per run;
- maximum provider calls per run;
- maximum total response bytes;
- maximum CPU units;
- maximum action risk;
- required reasoning mode;
- default promotion policy ID or domain-to-policy mapping;
- experiment-protocol selection policy;
- probation observation freshness limits;
- source revalidation intervals;
- enabled action-kind allowlist;
- deterministic policy signature.

Policy defaults must be conservative, resource-bound, and packaged as immutable
resources or registered records.

### 8.4 Plan

`InternetImprovementPlan` contains:

- plan ID and signature;
- cycle key;
- baseline event head and projection checkpoint digest;
- Director policy identity;
- normalized planning timestamp;
- eligible ordered actions;
- deferred actions with exact reasons;
- blocked subjects with exact reasons;
- allocated response-byte, CPU, provider-call, cost, and risk budgets;
- Director version.

The baseline event head is provenance, not a requirement that the entire event
head remain unchanged after the Orchestrator performs its own earlier action.
Each action uses local subject preconditions and is re-planned after mutation.

## 9. Receding-Horizon Lifecycle

The Director plans only the current executable frontier.

```text
read immutable state
        |
        v
direct current frontier
        |
        v
lease one action
        |
        v
revalidate -> execute -> receipt
        |
        v
read newly active state and re-plan
```

For one candidate lineage, only one state-changing action may be active at a
time. Independent fetch jobs may appear in the same plan, but the first release
executes them sequentially through one writer.

### 9.1 Candidate Advancement

The Director applies these rules:

1. A `VALIDATION_READY` candidate without a reasoning analysis for the current
   candidate content and reasoning policy receives `reason_candidate` when
   reasoning is required.
2. A `VALIDATION_READY` candidate with satisfied reasoning policy receives
   `qualify_candidate` only when an applicable experiment protocol exists.
3. Missing protocols produce `MISSING_EXPERIMENT_PROTOCOL`; they do not produce
   default expected outputs.
4. `EXPERIMENT_QUALIFIED` receives `assess_promotion` only when no assessment
   exists for the same candidate, qualification, and promotion policy.
5. A blocked assessment is not repeated until a material input identity changes.
6. `POLICY_QUALIFIED` receives `admit_probation` only once for the active
   candidate lineage.
7. `PROBATIONARY_CANONICAL` may receive `select_probation_candidate` for a real
   query request or `consume_probation_observation` for a registered observation
   input.
8. Missing probation input produces `WAITING_FOR_OBSERVATION`.
9. `CANONICAL`, `DEMOTED`, `EXPERIMENT_FAILED`, duplicate, related, and
   quarantined candidates are terminal for ordinary advancement until a new
   source, protocol, policy, observation, or improvement opportunity changes
   their eligibility.

### 9.2 Acquisition Advancement

The Director applies these rules:

1. recover expired fetch leases before scheduling replacement work;
2. schedule one logical fetch job per watch generation and interval;
3. select due work using existing deterministic scheduler limits;
4. fetch only under the latest active lease;
5. assess only snapshots with a successful or not-modified receipt;
6. extract only policy-admissible snapshots;
7. feed only extraction receipts not already consumed by a BrainFeed batch;
8. never fetch disabled or superseded watches;
9. never broaden source policy from internet content or model output.

### 9.3 Priority Classes

Director ordering is deterministic:

1. integrity failures and recoverable incomplete durable work;
2. active probation regression and source invalidation;
3. expired lease recovery;
4. due source freshness and revalidation;
5. candidate advancement with complete evidence protocols;
6. scheduled improvement opportunities;
7. ordinary watch polling;
8. advisory reasoning refresh explicitly enabled by policy.

Within a class, order by:

```text
(not_before,
 deadline,
 descending priority,
 ascending risk,
 source_group,
 subject_id,
 action_key)
```

## 10. Supporting Evidence Records

### 10.1 Source Assessment Input

Add `InternetSourceAssessmentInput` and object type
`internet-source-assessment-input` so robots and license facts are never
invented by the Director. The record binds the snapshot, fetch receipt, source
policy, robots result, license classification, evidence IDs, producer identity,
provenance, and signature.

### 10.2 Experiment Protocol

Add `InternetExperimentProtocol` and object type
`internet-experiment-protocol`.

Required fields include:

- protocol version and identity;
- applicable candidate classes, primitives, domains, and semantic signatures;
- baseline canonical reference and baseline SAA IR;
- frozen dataset snapshot IDs;
- exact trial groups, deterministic seeds, inputs, and trusted expected outputs;
- material-effect and numeric-domain bounds;
- minimum experiment and independence-group counts;
- benchmark scores and benchmark policy;
- knowledge-integrity snapshots and policy;
- validity interval and superseded protocol ID;
- source and authoring provenance;
- protocol signature.

The protocol is data. It cannot contain executable code, commands, scripts, or a
provider instruction that bypasses the existing exact experiment engine.

### 10.3 Probation Observation Input

Add `InternetProbationObservationInput` and object type
`internet-probation-observation-input`.

Required fields include:

- candidate and admission IDs;
- query and context signatures;
- observation time and window index;
- candidate and baseline outcome facts;
- invariant, benchmark, integrity, source, and reproduction results;
- regression signals;
- evidence IDs proving each asserted result;
- observation producer identity and provenance;
- observation-input signature.

The Orchestrator converts this record to the existing
`InternetProbationObservationRequest`; it does not calculate ungrounded boolean
outcomes itself.

### 10.4 Reasoning Lineage

Extend `InternetAlgorithmCandidate` with `reasoning_analysis_ids` through a
versioned migration. The reasoning coordinator appends the registered analysis
ID when it supersedes a candidate.

Director reasoning eligibility is keyed by:

- candidate content signature;
- source fragment identities;
- reasoning policy;
- provider/model identity policy;
- grammar and parser versions.

Provider failure with deterministic fallback is a completed advisory analysis,
not an infinite retry trigger, unless policy explicitly requests another
provider identity.

## 11. Orchestration Persistence

Add these immutable object types:

### 11.1 `internet-improvement-plan`

Stores the exact Director plan and signature.

### 11.2 `internet-improvement-run`

Declares one bounded run:

- run ID;
- plan ID;
- worker identity;
- requested budgets;
- start time;
- resume-of run ID when applicable;
- run signature.

### 11.3 `internet-improvement-run-event`

Append-only run state transitions:

- `STARTED`;
- `ACTION_LEASED`;
- `ACTION_COMPLETED`;
- `ACTION_SKIPPED`;
- `ACTION_FAILED`;
- `REPLANNED`;
- `BUDGET_EXHAUSTED`;
- `NO_ELIGIBLE_WORK`;
- `COMPLETED`;
- `ABANDONED`.

### 11.4 `internet-improvement-action-lease`

Follows the existing fetch-lease pattern:

- action key;
- run and worker IDs;
- acquired and expiry times;
- predecessor lease ID;
- attempt number;
- state;
- lease signature.

Only the latest active lease for an action key may finalize that action.

### 11.5 `internet-improvement-action-receipt`

Stores:

- action key, plan, run, and lease IDs;
- expected and observed preconditions;
- exact input IDs;
- exact output IDs;
- canonical executor version;
- provider/model identity where applicable;
- started and completed times;
- terminal state;
- typed error code and bounded diagnostic;
- `executed`, `reconciled`, `stale`, or `skipped` disposition;
- result signature.

## 12. Idempotency and Recovery

### 12.1 Acquisition

- Plan registration is idempotent by plan signature.
- Action identity excludes worker and attempt metadata.
- Lease registration rejects a concurrent active latest lease.
- Expired leases can be superseded by a new attempt.
- A stale lease cannot persist a terminal receipt.
- Content-addressed source artifacts and snapshots deduplicate identical bytes.
- Repeated HTTP requests after a crash may occur and must be recorded honestly.

### 12.2 Candidate Mutation

Before candidate execution, the Orchestrator verifies:

- the candidate object exists and has the expected type;
- it is still an active candidate ID;
- its status and candidate signature match the action precondition;
- policy, protocol, source, and qualification records still match;
- no terminal receipt already exists for the same action key.

After a crash, if the expected superseding candidate or stage output already
exists, the Orchestrator records a reconciled receipt instead of executing the
stage again.

### 12.3 Failure Classes

Failures are typed as:

- `TRANSIENT_PROVIDER_FAILURE`;
- `TRANSIENT_NETWORK_FAILURE`;
- `LEASE_EXPIRED`;
- `STALE_SUBJECT`;
- `MISSING_PROTOCOL`;
- `MISSING_OBSERVATION`;
- `POLICY_BLOCKED`;
- `QUALIFICATION_FAILED`;
- `INTEGRITY_FAILURE`;
- `CONTRACT_FAILURE`;
- `PERMANENT_SOURCE_REJECTION`;
- `INTERNAL_FAILURE`.

Only transient failures are automatically retryable, and their retry ceilings
are policy-bound.

## 13. Process and Lock Model

`EgcfStore` currently holds a nonblocking exclusive workspace lock for the
lifetime of each store instance. The first Director/Orchestrator release will
therefore use a bounded single-process, single-writer `run-once` model.

The release will not claim a continuously running multi-process daemon.

Recommended operation:

```text
system timer
    -> statewright run-json internet-improvement/run-once request
    -> bounded plan and execution
    -> durable receipts
    -> process exit and lock release
```

Parallel network fetch and long-running service mode are deferred until a
separate store-session refactor can release the EGCF lock during external I/O
while preserving atomic lease acquisition and finalization.

## 14. CLI Contract

Extend the existing `internet-improvement` operation with:

- `plan` — return and optionally register the current signed plan;
- `run-once` — execute a bounded run;
- `resume` — reconcile and continue a prior nonterminal run;
- `run-status` — inspect plans, runs, events, leases, and receipts;
- `explain-action` — show eligibility, dependencies, and blocking reasons.

Retain all existing actions:

- `status`;
- `advance`;
- `feed`;
- `reason`;
- `experiment-qualify`;
- `policy-assess`;
- `probation-admit`;
- `probation-select`;
- `probation-observe`.

`advance` becomes a compatibility adapter equivalent to a candidate-scoped
`run-once` with `max_actions = 1`. It must delegate to the Director rather than
retaining an independent status switch.

All command contracts must be added to:

- `resources/commands/v1/catalog.json`;
- `resources/commands/v1/contracts.json`;
- CLI help and dispatch;
- command contract tests and package smoke coverage.

## 15. Schema and Migration Plan

Do not modify the frozen EGCF v1 base schemas.

Extend `resources/schemas/statewright-v1/internet-improvement-extension.schema.json`
with the new records and fields.

Add ordered migrations after the existing `0007` migration:

1. `0008-internet-orchestration-record-extension.json`
2. `0009-internet-experiment-protocol-extension.json`
3. `0010-internet-candidate-reasoning-lineage.json`

Migration requirements:

- old candidate IDs remain valid immutable records;
- candidate migration produces a new superseding record with
  `reasoning_analysis_ids` defaulting to an empty array;
- existing promotion and probation decisions retain original identities;
- projections rebuild from immutable objects without editing old records;
- old CLI requests remain valid;
- migration is idempotent and source-manifest bound.

Projection version must increase only if new indexed fields or tables are
required. New projection state must remain derived and rebuildable.

## 16. File Change Inventory

### 16.1 New production files

- `Core/include/statewright/egcf/internet_improvement_director.hpp`
- `Core/src/egcf/internet_improvement_director.cpp`
- `Core/include/statewright/egcf/internet_improvement_orchestrator.hpp`
- `Core/src/egcf/internet_improvement_orchestrator.cpp`
- `Core/include/statewright/egcf/internet_source_coordinator.hpp`
- `Core/src/egcf/internet_source_coordinator.cpp`

### 16.2 New tests

- `Tests/egcf/test_internet_improvement_director.cpp`
- `Tests/egcf/test_internet_improvement_orchestrator.cpp`
- `Tests/egcf/test_internet_source_coordinator.cpp`
- `Tests/internet_orchestrator_cli_smoke.sh`
- `Tests/internet_orchestrator_fault_smoke.sh`

### 16.3 Modified production and contract files

- `Core/include/statewright/egcf/internet_records.hpp`
- `Core/src/egcf/internet_records.cpp`
- `Core/include/statewright/egcf/internet_improvement_store.hpp`
- `Core/src/egcf/internet_improvement_store.cpp`
- `Core/src/egcf/internet_reasoning.cpp`
- `Core/src/egcf/store.cpp` only if projection indexes require changes;
- `Apps/statewright/cli.cpp`
- `CMakeLists.txt`
- `resources/schemas/statewright-v1/internet-improvement-extension.schema.json`
- `resources/commands/v1/catalog.json`
- `resources/commands/v1/contracts.json`
- `resources/manifest.sha256`
- `contracts/migrations/0008-internet-orchestration-record-extension.json`
- `contracts/migrations/0009-internet-experiment-protocol-extension.json`
- `contracts/migrations/0010-internet-candidate-reasoning-lineage.json`

### 16.4 Documentation and release files

- `README.md`
- `docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md`
- `docs/SAA_INTERNET_IMPLEMENTATION_AUDIT.md`
- `docs/RELEASE_QUALIFICATION.md`
- `docs/RESIDUAL_RISKS.md`
- `Tools/generate_internet_release_evidence.sh`
- `Tests/saa_internet_howto_smoke.sh`

## 17. Implementation Phases

### Phase D0 — Freeze and Inventory

Work:

- record current commit, branch, dirty state, toolchain, and resource manifest;
- inventory all existing internet commands, object types, migrations, candidate
  statuses, and coordinator preconditions;
- freeze current CLI fixture outputs for compatibility comparison;
- confirm no active writer is changing `.ourd-agent` evidence during broad
  validation.

Gate:

- exact source baseline and compatibility inventory are recorded;
- unrelated dirty work, if any, is preserved and explicitly excluded.

### Phase D1 — Record and Contract Foundations

Work:

- define Director, plan, run, run-event, action-lease, action-receipt,
  experiment-protocol, and probation-input C++ records;
- implement canonicalization, JSON conversion, parsing, signatures, and typed
  object IDs;
- add extension schema definitions and migrations;
- add round-trip, canonical-order, malformed-input, and identity tests.

Gate:

- all new records are deterministic and schema-valid;
- no base EGCF v1 schema changes;
- old fixture identities remain unchanged.

### Phase D2 — Store Read and Persistence Surface

Work:

- extend `InternetImprovementStore` with registration and query methods;
- add active candidate, latest action lease, terminal receipt, protocol lookup,
  probation-input lookup, and run-status helpers;
- preserve one EGCF persistence root and one supersedence model;
- add integrity and projection rebuild tests.

Gate:

- duplicate registration is idempotent;
- stale lease finalization is rejected;
- projection rebuild exactly matches immutable objects.

### Phase D3 — Pure Director

Work:

- implement normalized state construction;
- implement closed action algebra;
- compose source scheduler and SAA improvement scheduler;
- implement deterministic ordering, budgets, deferrals, and blocking reasons;
- implement candidate, acquisition, revalidation, and probation eligibility;
- remove reliance on status alone for reasoning completion.

Gate:

- identical state and policy produce byte-identical plan JSON and signature;
- permutation of input record order does not change the plan;
- missing protocol and observation evidence fail closed;
- blocked assessments are not repeatedly selected for unchanged inputs.

### Phase D4 — Source Coordinator Refactor

Work:

- extract fetch, capture, source assessment, and extraction orchestration from
  the CLI;
- preserve existing response envelopes, failures, lease checks, and object IDs;
- make the current CLI call the new coordinator;
- run differential CLI fixtures before adding the Orchestrator.

Gate:

- existing internet fetch/source/extract tests remain byte-compatible where the
  response is contractually stable;
- downloaded content is still never executed.

### Phase D5 — One-Action Orchestrator

Work:

- implement `run_once` with one action, one lease, one canonical executor call,
  and one terminal receipt;
- support injected clock, provider, worker identity, and cancellation predicate;
- revalidate local subject preconditions before execution;
- reconcile already-completed deterministic actions.

Gate:

- every action kind has exactly one canonical executor mapping;
- no generic command execution path exists;
- repeated `run_once` is idempotent for unchanged terminal work.

### Phase D6 — Replanning and Bounded Runs

Work:

- add receding-horizon re-planning after each successful mutation;
- enforce action, time, byte, CPU, risk, and provider-call budgets;
- register run events and terminal summaries;
- stop deterministically on no work, budget exhaustion, blocked evidence, or
  integrity failure.

Gate:

- one candidate never receives two concurrent state-changing actions;
- newly superseded candidate IDs are used after each re-plan;
- run summaries are reconstructable from immutable records.

### Phase D7 — Resume and Fault Reconciliation

Work:

- implement expired action-lease recovery;
- implement crash reconciliation before and after every durable boundary;
- detect outputs persisted without a terminal action receipt;
- reject stale workers and stale subject identities;
- add typed retry ceilings and permanent-failure handling.

Gate:

- crash injection at every durable boundary preserves integrity;
- no stale lease can finalize;
- duplicate terminal domain records are not created;
- HTTP retry behavior is recorded as at-least-once rather than exactly-once.

### Phase D8 — CLI Integration and Compatibility

Work:

- add `plan`, `run-once`, `resume`, `run-status`, and `explain-action`;
- route `advance` through the Director and one-action Orchestrator;
- update command resources, help, examples, and error envelopes;
- preserve direct manual stage actions.

Gate:

- old `advance` requests remain valid;
- plan and status defaults are read-only;
- no `approve` action is added;
- package installation exposes the same command contracts as the build tree.

### Phase D9 — Security, Property, and Performance Qualification

Work:

- test hostile content attempting to emit actions, policies, commands, or model
  instructions;
- test deterministic planning under reordered records and clocks;
- test lease races, stale generations, repeated blocked assessments, missing
  protocols, missing observations, provider failures, and projection corruption;
- test bounded memory, action count, response-byte, CPU, and provider-call use;
- test one-writer timer overlap behavior.

Gate:

- hostile internet or model content cannot alter the action allowlist;
- planner determinism and idempotency properties pass;
- a second overlapping run fails or defers safely without corrupting state.

### Phase D10 — Documentation and Release Evidence

Work:

- add manual and timer-driven HOWTO procedures;
- document experiment protocol and probation observation provenance;
- document recovery, reconciliation, lock behavior, and residual limitations;
- update implementation audit, release qualification, and risk register;
- extend internet release evidence with exact plan/run/receipt fixtures;
- run developer, sanitizer, release, package, CLI, HOWTO, integrity, migration,
  fault, and source-manifest validation.

Gate:

- a clean package runs the complete fixture-scope orchestration lifecycle;
- evidence is bound to exact source, dependency, schema, migration, policy,
  protocol, and test hashes;
- release text does not claim a multi-process daemon, exactly-once HTTP, general
  internet search, arbitrary code execution, or expanded EGCF authority.

## 18. Test Matrix

### 18.1 Unit tests

- action and plan canonicalization;
- stable signatures and typed IDs;
- Director state normalization;
- deterministic action ordering;
- budget accounting;
- candidate reasoning completion;
- protocol applicability;
- probation observation matching;
- latest action lease and expiry;
- terminal receipt uniqueness;
- run-event reconstruction.

### 18.2 Integration tests

- watch to fetch to snapshot to extraction to candidate;
- candidate reasoning to experiment to policy to probation;
- real observation input to canonical promotion;
- regression observation to demotion and previous-preference restoration;
- blocked policy assessment followed by new evidence and re-assessment;
- failed experiment followed by a new protocol version;
- projection rebuild followed by identical Director plan.

### 18.3 Fault tests

Inject interruption:

- after plan registration;
- after run registration;
- after lease acquisition;
- before external provider call;
- after provider return but before domain registration;
- after domain registration but before action receipt;
- after action receipt but before lease closure;
- after lease closure but before run event;
- during projection update;
- during resume reconciliation.

### 18.4 Security tests

- downloaded prompt injection requesting new actions;
- source text containing shell commands and executable paths;
- provider output claiming policy approval;
- provider output supplying fabricated evidence IDs;
- protocol containing unsupported executable material;
- private, loopback, link-local, or redirect-rebound fetch attempts;
- oversized and decompression-amplified responses;
- stale source evidence and tampered artifacts.

### 18.5 Compatibility tests

- current CLI request fixtures;
- current object and candidate fixtures;
- existing migration chain from pre-internet and internet schema generations;
- existing package smoke and HOWTO smoke;
- resource manifest and source-manifest parity.

## 19. Release Criteria

Implementation is complete only when:

1. Director routing is no longer independently implemented in the CLI.
2. Identical authoritative state and policy produce identical plans.
3. Every executed action has a valid latest lease and terminal receipt.
4. Restart recovery passes every planned crash point.
5. Stale candidates, policies, protocols, watches, and leases fail closed.
6. Missing experiment protocols and probation observations produce explicit
   deferrals without fabricated evidence.
7. OIEC-SR provider output remains non-authoritative.
8. Normal SAA promotion remains approval-free and deterministic.
9. Existing EGCF execution authority is unchanged.
10. Existing manual internet operations and `advance` remain compatible.
11. The packaged `run-once` path passes fixture-scope end to end.
12. Developer, sanitizer, release, package, CLI, migration, integrity, fault, and
    documentation tests pass.
13. Release evidence is frozen against a stable source commit and records all
    relevant hashes and residual risks.

## 20. Rollback and Compatibility

Rollback is performed by disabling the new `run-once` entry point and continuing
to use existing manual lifecycle actions. Immutable plans, runs, leases, and
receipts remain valid historical evidence and are not deleted.

If the Director produces an unsafe or incorrect selection:

- stop timed invocation;
- preserve the exact plan, policy, state signature, and event head;
- register the fault and corrected policy or Director version;
- rebuild derived projections if necessary;
- resume only from a new signed plan.

Existing candidate, qualification, promotion, probation, and canonical records
must never be rewritten to simulate rollback.

## 21. Explicitly Deferred Work

- continuously running daemon mode;
- distributed or multi-host workers;
- parallel store writers;
- authenticated browsing or ambient credentials;
- general search-engine discovery;
- JavaScript browser execution;
- downloaded program compilation or execution;
- model training or fine-tuning;
- arbitrary command or workspace mutation;
- automatic production ownership cutover;
- exactly-once HTTP guarantees.
