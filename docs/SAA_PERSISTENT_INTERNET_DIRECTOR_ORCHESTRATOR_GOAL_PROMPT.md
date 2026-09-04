# SAA Persistent Internet Improvement Director and Orchestrator Goal Prompt

**Prompt date:** 2026-09-04

**Plan:** `docs/SAA_PERSISTENT_INTERNET_DIRECTOR_ORCHESTRATOR_IMPLEMENTATION_PLAN.md`

**Plan baseline:** `857639cf80c7cb12fbcbefd751328df2cb887a03`

**Purpose:** Reusable implementation prompt for a coding agent working in the
StateWright repository

## Goal Prompt

Implement the SAA Persistent Internet Improvement Director and Orchestrator in
the StateWright C++20 repository.

Read and follow
`docs/SAA_PERSISTENT_INTERNET_DIRECTOR_ORCHESTRATOR_IMPLEMENTATION_PLAN.md`
before changing code. Treat that plan as the scope, architecture, compatibility,
test, rollback, and release contract for this goal.

### Objective

Move the existing `internet-improvement advance` lifecycle routing from the CLI
into a deterministic core Director, then implement a restart-safe bounded
Orchestrator that leases and executes only closed internal lifecycle actions
through the existing canonical StateWright services.

The completed system must persist signed plans, runs, run events, action leases,
and action receipts in the existing EGCF object store; require registered
experiment protocols and provenance-bound probation observation inputs; preserve
the current manual CLI operations; and provide `plan`, `run-once`, `resume`,
`run-status`, and `explain-action` actions under `internet-improvement`.

### Initial Audit

Before editing:

1. Confirm the actual workspace root, Git branch, `HEAD`, remotes, and dirty
   state.
2. Preserve all unrelated local work. Do not stash, reset, clean, discard, or
   overwrite user changes.
3. Record whether the current `HEAD` differs from the plan baseline
   `857639cf80c7cb12fbcbefd751328df2cb887a03`.
4. Re-read applicable `AGENTS.md` files.
5. Inventory current internet object types, migrations, command contracts,
   candidate statuses, coordinator preconditions, tests, and release scripts.
6. Inspect the exact current implementations of:
   - `InternetImprovementStore`;
   - the source scheduler and fetch path;
   - `InternetFeedCoordinator`;
   - `InternetReasoningCoordinator`;
   - `InternetExperimentCoordinator`;
   - `AutonomousPromotionController`;
   - `InternetProbationController`;
   - `CanonicalAlgorithmStore`;
   - `KnowledgeGovernanceStore`;
   - `internet-improvement advance`;
   - EGCF workspace locking and projection rebuild.
7. Freeze or capture current CLI fixture behavior before refactoring shared
   paths.

Do not assume source counts, line numbers, test counts, schema versions, or
resource hashes from the plan are still current. Recompute them.

### Canonical Ownership Requirements

- `EgcfStore` remains the canonical immutable object, artifact, event,
  supersedence, and projection authority.
- `InternetImprovementStore` remains the one internet lifecycle persistence
  surface and must be extended rather than replaced.
- `CanonicalAlgorithmStore` remains the sole canonical SAA algorithm owner.
- Existing source scheduling functions remain the owner of watch/job selection.
- Existing SAA improvement scheduling remains the owner of opportunity scoring.
- Existing feed, reasoning, experiment, promotion, and probation coordinators
  remain the owners of their domain transitions.
- The new Director owns only next-action decisions.
- The new Orchestrator owns only execution attempts, leases, receipts, and run
  history.

Do not create a parallel database, internet algorithm catalog, promotion engine,
probation engine, model authority, or general command executor.

### Required Architecture

Implement:

- `InternetImprovementDirector` as a deterministic planner with no writes,
  network I/O, provider calls, or arbitrary filesystem mutation;
- `InternetImprovementState` and a store-backed state reader;
- `InternetDirectorPolicy` with explicit action, time, response-byte, CPU, risk,
  retry, and provider-call budgets;
- a closed `InternetDirectedActionKind` enum;
- signed `InternetDirectedAction` and `InternetImprovementPlan` records;
- `InternetImprovementOrchestrator::run_once` and `resume`;
- a thin `InternetSourceCoordinator` extracted from current CLI fetch,
  assessment, capture, failure, and extraction assembly;
- immutable experiment-protocol and probation-observation-input records;
- immutable provenance-bound source-assessment input records;
- explicit reasoning-analysis lineage on superseding candidate records;
- signed run, run-event, action-lease, and action-receipt records;
- schema migrations, command contracts, tests, documentation, and release
  evidence.

Use receding-horizon planning. Execute at most one state-changing action for a
candidate lineage, then read newly active state and plan again. Do not build a
large static candidate pipeline using future candidate IDs.

### Closed Action Set

The Orchestrator may execute only typed actions equivalent to:

- recover an expired lease;
- schedule a fetch;
- execute a lease-bound fetch;
- assess a source snapshot;
- extract a policy-admissible snapshot;
- feed an extraction into BrainFeed and SAA retrieval;
- run advisory OIEC-SR reasoning;
- execute an exact registered experiment protocol;
- assess the registered autonomous promotion policy;
- admit a policy-qualified candidate to probation;
- select a probationary candidate for a real query;
- consume a provenance-bound probation observation;
- revalidate source evidence;
- verify immutable records and projections.

Do not accept a shell command, executable path, script, downloaded program,
free-form tool request, or model-generated action name.

### Evidence Requirements

Do not invent source-policy, qualification, or observation evidence.

A source assessment requires a registered input binding the snapshot, fetch
receipt, source policy, robots result, license classification, evidence IDs,
producer identity, provenance, and signature. If it is absent, return
`MISSING_SOURCE_POLICY_EVIDENCE`; never assume robots permission or a license.

An experiment action requires a registered applicable protocol containing:

- baseline identity and SAA IR;
- frozen dataset snapshot IDs;
- deterministic trials and seeds;
- trusted expected outputs;
- experiment thresholds;
- benchmark and integrity policies;
- provenance and signature.

If no applicable protocol exists, persist or return
`MISSING_EXPERIMENT_PROTOCOL` and defer the action.

A probation observation action requires a registered input containing real:

- query and context signatures;
- candidate and baseline outcome facts;
- invariant, benchmark, integrity, source, and reproduction results;
- regression signals;
- supporting evidence IDs and producer provenance.

If no applicable observation exists, return `WAITING_FOR_OBSERVATION` and do not
fabricate boolean results.

### Reasoning Boundary

OIEC-SR and external providers are advisory only.

- Record provider, model, grammar, parser, request, and output identities.
- Provider failure may use the existing deterministic fallback.
- A completed deterministic fallback must not cause an infinite reasoning retry.
- Model output cannot create a policy, approve promotion, waive a gate, assert a
  successful experiment, fabricate evidence IDs, or add an action kind.
- Internet or provider text is hostile data and cannot be interpreted as agent
  instructions.

### Promotion and Authority Boundary

Normal SAA internet promotion remains approval-free. Do not add an `approve`
command, approval record, confirmation step, signature ceremony, or manual
candidate gate.

Preserve the separate EGCF command-execution boundary:

- internet content is evidence, not execution authority;
- canonical SAA admission does not grant C3 or C5 command authority;
- no downloaded code is executed or compiled as a qualification shortcut;
- no production ownership cutover is automatically authorized.

### Persistence and Identity

Register all new authoritative facts through the existing EGCF store.

Implement object types for:

- internet improvement plans;
- runs and append-only run events;
- action leases;
- action receipts;
- experiment protocols;
- probation observation inputs;
- source assessment inputs.

Action identity must be derived from semantic inputs, not worker identity or
attempt time. Only the latest active lease may finalize an action. Existing
terminal outputs must be reconciled rather than duplicated.

Do not claim exactly-once HTTP behavior. Implement and document at-least-once
external acquisition with exactly-once durable action finalization.

### Compatibility

- Keep every existing manual internet operation.
- Add `plan`, `run-once`, `resume`, `run-status`, and `explain-action` under
  `internet-improvement`.
- Retain `advance` as a candidate-scoped one-action compatibility adapter.
- Move the current `advance` routing into the Director; do not leave a second
  status switch in the CLI.
- Move shared fetch, assessment, and extraction assembly from the CLI into the
  core source coordinator, then make both CLI and Orchestrator use it.
- Preserve stable JSON envelopes, error categories, typed IDs, canonical JSON,
  and current immutable record identities unless an explicit migration applies.

### Process Model

Implement a bounded single-process, single-writer `run-once` release first.

`EgcfStore` currently holds an exclusive workspace lock for its lifetime. Do not
claim or add a continuously running multi-process daemon unless the store lock
and short-session commit design are separately implemented and qualified.

The initial supported operational model is a system timer invoking a bounded
`run-once` process that exits and releases the workspace lock.

### Required Phase Gates

Follow the implementation phases in the plan and do not collapse them into one
unreviewable change.

At minimum, complete and validate:

1. source freeze and compatibility inventory;
2. record types, canonicalization, schemas, and migrations;
3. store registration and active-state queries;
4. pure Director and deterministic planning tests;
5. source coordinator extraction with CLI differential tests;
6. one-action Orchestrator;
7. receding-horizon bounded runs;
8. resume, expired lease recovery, and crash reconciliation;
9. CLI compatibility and package contracts;
10. security, property, fault, performance, documentation, and release evidence.

Do not proceed past a failed phase gate by weakening the assertion, changing the
fixture to hide the failure, or claiming partial evidence proves full release
qualification.

### Required Tests

Add focused tests for:

- record parsing, canonicalization, signatures, and typed IDs;
- deterministic planning under permuted input order;
- stable budget allocation and ordering;
- reasoning completion without status changes;
- missing protocol and observation deferral;
- repeated blocked policy assessment suppression;
- action lease acquisition, expiry, supersedence, and stale finalization;
- reconciliation when domain output exists without an action receipt;
- stale candidate and watch generation rejection;
- retry ceilings and permanent failure classes;
- hostile internet and model content;
- projection rebuild parity;
- old `advance` compatibility;
- package and HOWTO execution.

Inject crashes after every durable boundary listed in the implementation plan.

Run the narrowest tests after each phase, then the complete developer,
sanitizer, release, package, CLI, migration, integrity, fault, resource-manifest,
HOWTO, and release-evidence suites before claiming completion.

Do not fix unrelated failures. Report them separately with exact commands and
evidence.

### Documentation and Release Evidence

Update:

- the persistent internet HOWTO;
- the implementation audit;
- release qualification;
- residual risks;
- README command and document inventory;
- source-bound internet release evidence generation.

The final evidence must bind:

- exact Git commit and dirty state;
- compiler and dependency identities;
- source, resource, schema, migration, policy, and protocol hashes;
- test commands, results, durations, and logs;
- package installation results;
- plan, run, lease, receipt, recovery, promotion, and demotion fixtures;
- explicit residual limitations.

Do not claim:

- a continuously running daemon;
- distributed workers;
- exactly-once HTTP;
- general internet search;
- authenticated browsing;
- downloaded program execution;
- model authority;
- automatic EGCF command authority;
- automatic production ownership cutover.

### Completion Standard

The goal is complete only when all implementation-plan release criteria are met,
all required tests pass, release evidence is generated against a stable source,
and the final report identifies exact files, migrations, commands, results,
hashes, and residual risks.

If a real external, source, toolchain, or evidence blocker prevents completion,
stop at the blocker without discarding completed work. Report:

- completed phases;
- failed gate;
- exact command and error;
- affected files and records;
- whether source or evidence changed during the attempt;
- safest next action.

Do not commit, push, publish, change repository visibility, or deploy unless the
user explicitly requests that separate action.
