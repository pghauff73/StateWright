# StateWright Minimal Unified C++ Refactor Implementation Plan

**Plan date:** September 2, 2026  
**Target:** `/home/pamela/Projects/StateWright`  
**Language baseline:** C++20  
**Plan status:** Candidate implementation plan; not implementation, migration approval, qualification, or release approval  
**Source oracle:** `/home/pamela/Projects/OIEC-STM-Agent-integration`  
**Recorded source commit:** `957111e2d5d11dec719c7f993f51644e701fc256`  

## 1. Executive Decision

StateWright will be a minimal, unified C++20 implementation of the governed
reasoning and engineering-command spine currently distributed across the
OIEC-STM-Agent Python implementation.

The scoped product will include:

1. governed workspace inspection and mutation;
2. authority, policy, evidence, approval, transaction, verification, rollback,
   event, collision, and bounded-loop enforcement;
3. operational hypothesis state used by the production control loop;
4. the OIEC-SR bounded multi-hypothesis reasoning kernel;
5. EGCF typed commands, capability resolution, algorithm qualification,
   workflow compilation, simulation, approval, execution, assurance, replay,
   and failure learning;
6. the searchable algebra of algorithms, including canonical algorithm IR,
   normalization, dynamics, MIMO, representative forms, semantic alignment,
   nonlinear relations, reasoning relations, unified retrieval, transfer,
   adaptation, experiments, failure algebra, and improvement scheduling;
7. a bounded provider protocol for model-generated proposals; and
8. a native CLI, a migration shadow protocol, deterministic validation tools,
   and release evidence.

The scoped product will not port the complete OIEC-STM-Agent application. It
will exclude formal writing, document ingestion, OCR, PDF processing, corpus
summarization, the Tk/Qt workbench, OpenGL tooling, browser operation, remote
service orchestration, and documentation-site generation.

This is a semantic authority migration, not a line-by-line translation and not
a permanently parallel implementation. Python remains the oracle until each
C++ owner passes contract, parity, fault, cutover, rollback, and observation
gates. After cutover, StateWright becomes the sole authoritative owner for the
scoped behavior.

## 2. Baseline and Source-Freeze Requirement

### 2.1 Target baseline

The StateWright target currently contains only an empty `Core/` directory. No
existing C++ API, persistent format, build graph, or compatibility surface is
authoritative.

### 2.2 Source baseline

The recorded source checkout is at commit
`957111e2d5d11dec719c7f993f51644e701fc256`, matching `origin/main` at the time
this plan was written. The checkout is not a clean source oracle: it contains
modified EGCF and formal-writing files, untracked implementation plans, new
improvement modules, tests, and report artifacts.

Implementation must not silently combine the commit with the dirty working
tree. Phase SW0 must select and freeze one exact source state before fixtures,
requirements, schemas, or parity claims are generated.

### 2.3 Inspected scope

The currently inspected source contains approximately:

- 21 Python modules under `ourd/reasoning/`;
- 105 Python modules under `ourd/egcf/`;
- 47 Python modules under `ourd/egcf/algebra/`;
- 43,659 lines across the reasoning and EGCF Python trees;
- 3,947 additional lines across the primary governance, workspace,
  persistence, transaction, policy, OIEC-control, loop-control, and operational
  hypothesis owners;
- 35 directly named reasoning, hypothesis, EGCF, and SAA test files containing
  at least 342 declared test functions or test classes; and
- language-neutral command, algorithm, workflow, schema, and reasoning
  benchmark resources.

These counts are discovery evidence only. SW0 must regenerate the authoritative
inventory from the frozen source.

## 3. Product Boundary

### 3.1 In scope

#### Governed runtime

- canonical paths and workspace scopes;
- source snapshots and file identities;
- authority manifests and capability grants;
- deterministic policy and risk floors;
- immutable evidence artifacts;
- approval records bound to exact object and plan hashes;
- prepared candidate transactions;
- source-drift detection;
- atomic apply, post-write verification, finalization, exact rollback, and
  restart recovery;
- append-only hash-chained events;
- rebuildable runtime projections;
- collision identity, failure records, retry discipline, and cycle prevention;
- bounded operational hypothesis state; and
- exact-argv local process execution under policy.

#### OIEC-SR reasoning and hypothesis machinery

- `ReasoningProblem` and bounded reasoning budgets;
- the production operational `HypothesisSet` and the distinct OIEC-SR
  reasoning hypothesis aggregate;
- deterministic hypothesis updates with evidence provenance;
- reasoning paths, steps, nodes, edges, inference modes, and topology;
- structured candidate generation;
- deterministic verifier adapters;
- adversarial falsification and counterexample records;
- contradiction lifecycle;
- diversity selection;
- versioned deterministic scoring and ranking;
- bounded search;
- synthesis and synthesis verification;
- causal and dimensional reasoning adapters where frozen fixtures require
  them;
- reasoning context projection;
- reasoning certificates;
- ablation runs, benchmark manifests, source-bound results, qualification, and
  eligibility decisions; and
- provider-bounded multi-candidate generation.

#### EGCF command fabric

- typed intent, command definition, and command invocation records;
- universal command context and modifiers;
- capability classes and scoped facets;
- algorithm definitions and exact implementation identities;
- contextual qualification and deterministic selection;
- evidence requirements, claims, confidence assessments, invariants, and
  decisions;
- workflow definitions and immutable compiled DAGs;
- simulation and replay;
- approval-bound execution plans;
- execution, rollback, failure, assurance, artifact, and supersedence records;
- exact lifecycle transitions;
- registry, catalog, compiler, handlers, adapters, brain feed, repository feed,
  and CLI behavior required by the frozen scope; and
- integration with StateWright transactions as the only workspace mutation
  path.

#### Searchable algebra of algorithms

- algorithm primitives and typed operand references;
- canonical algorithm IR and graph validation;
- normalization contracts and provenance;
- linear dynamics and MIMO structures;
- representative inputs, zeros, and canonical representative forms;
- semantic concepts, units, ontology, alignment, and revision;
- nonlinear local, global, geometric, lift, transform, control, stability, Lie,
  jet, evidence, and remainder records;
- reasoning semantics, equivalence, fit, composition, and outcome relations;
- lexical, semantic, structural, mathematical, reasoning, qualification, and
  outcome-aware retrieval;
- retrieval explanations and deterministic exclusions;
- cross-domain transfer assessment;
- bounded algorithm adaptation;
- multi-step lineage and invariant preservation;
- controlled algorithm experiments and aggregation;
- canonical failure algebra;
- OIEC-Bench admission gates;
- knowledge-integrity and promotion-governance records;
- closed intelligence-improvement records; and
- deterministic improvement scheduling.

### 3.2 Explicitly out of scope

- formal-writing and ICPI writing pipelines;
- PDF, OCR, page-label, citation, and document-source processing;
- full corpus ingestion and summarization;
- GUI migration;
- OpenGL, image, mesh, and visual-workbench features;
- remote execution, arbitrary MCP exposure, and distributed agents;
- browser automation;
- general shell access outside exact registered executor actions;
- macOS and Windows release qualification;
- model training or fine-tuning;
- opaque vector retrieval as canonical ranking authority; and
- automatic production authority cutover.

## 4. Non-Negotiable Invariants

1. **One canonical owner per semantic fact.** Runtime reasoning hypotheses,
   operational hypotheses, algebraic reasoning representations, command
   selection, and workspace mutation must have distinct owners and explicit
   adapters.
2. **StateWright is the sole mutation boundary.** EGCF and OIEC-SR may propose
   plans or actions, but all workspace mutation passes through the governed
   transaction service.
3. **No model authority.** A model may propose hypotheses, candidates,
   algorithms, relations, or changes. It may not establish identity, evidence
   validity, qualification, approval, risk reduction, mutation authority,
   benchmark eligibility, promotion, or release status.
4. **Exact identity precedes optimization.** Canonical JSON, typed IDs,
   SHA-256 hashes, signatures, schema versions, source snapshots, plan IDs,
   transaction IDs, reasoning certificates, algorithm IDs, and benchmark
   checksums must match before performance changes are accepted.
5. **Fail closed.** Unknown schemas, fields, inference modes, object types,
   capabilities, algorithms, relations, paths, migrations, or optional
   dependencies deny the operation with a stable diagnostic.
6. **Finite reasoning state.** Hypotheses, paths, steps, topology nodes and
   edges, provider calls, tool calls, tokens, elapsed time, and memory are
   bounded by deterministic limits.
7. **Structured reasoning only.** Persist reviewable claims, premises,
   evidence references, assumptions, inference modes, predictions, falsifiers,
   verifier results, contradictions, scores, and conclusions. Do not request or
   store private chain-of-thought.
8. **Evidence provenance is mandatory.** Factual premises and empirical
   conclusions bind to immutable evidence, deterministic adapter output, or an
   explicit assumption.
9. **No blind retry.** Regenerated prose, reordered candidates, or new random
   IDs do not unlock a failed action. Retry requires material new evidence,
   changed source, a changed bounded experiment, or another recorded epistemic
   change.
10. **Capability requirements form a union.** A composite command inherits the
    requirements of every reachable command, algorithm, executor, and child.
11. **Effective authority is an intersection.** External grants, requested
    scope, inherited constraints, policy ceilings, and approval constraints can
    only reduce effective authority.
12. **Canonical history is immutable.** Events and content-addressed objects
    are append-only. Supersedence creates a new object and relation rather than
    rewriting history.
13. **SQLite is never authority.** It is a rebuildable query and search
    projection derived from immutable canonical records.
14. **Search is explainable.** Every retrieval decision records the candidate
    set, exclusions, score components, tie-break rule, evidence, freshness, and
    qualification state.
15. **Exact arithmetic is preserved.** Rational values use arbitrary-precision
    integer numerators and denominators. Decimal or floating-point adapters use
    frozen precision and tolerance policies.
16. **Rollback remains executable.** Every mutating workflow has an exact or
    explicitly classified compensating rollback path, tested before authority
    moves.
17. **Generated evidence is not source.** Reports, caches, virtual
    environments, projections, and generated documentation cannot silently
    become migration inputs.
18. **Focused green tests do not prove completion.** Release requires the full
    frozen requirement matrix, parity inventory, fault suite, benchmark gates,
    package checks, rollback evidence, and explicit human approval.

## 5. Target Architecture

### 5.1 Build and language baseline

- C++20 with compiler extensions disabled;
- CMake as the only build graph;
- CTest as the test dispatcher;
- presets for developer, sanitizer, coverage, release, and provider-enabled
  builds;
- warnings as errors for StateWright source;
- deterministic build-information records;
- pinned dependencies with version, source, license, checksum, and update
  procedure; and
- Linux as the initial qualified platform.

### 5.2 Libraries and ownership

| Target | Canonical responsibility |
| --- | --- |
| `statewright_common` | result/error types, bounded values, canonical strings, UTF-8, timestamps, build identity |
| `statewright_contracts` | typed IDs, versioned durable records, canonical JSON serializers, strict validators |
| `statewright_core` | workspace, authority, policy, evidence, approval, events, projections, transactions, rollback, loop control, operational hypotheses |
| `statewright_reasoning` | OIEC-SR records, hypothesis updates, topology, verification, falsification, contradictions, scoring, bounded search, synthesis, certificates, benchmarks |
| `statewright_saa` | canonical algorithm IR, normalization, dynamics, MIMO, representative forms, nonlinear algebra, semantic and reasoning relations, transfer, adaptation, experiments, failure algebra |
| `statewright_egcf` | typed commands, capabilities, registries, qualification, deterministic retrieval, workflow compilation, lifecycle, simulation, assurance, replay, improvement scheduling |
| `statewright_provider_api` | read-only, versioned proposal and candidate interfaces |
| `statewright_providers` | subprocess provider adapter and optional exact-version llama.cpp adapter |
| `statewright_application` | dependency composition and end-to-end use cases without owning lower-level facts |
| `statewright` | human CLI and stable JSON output |
| `statewright-shadow` | migration-only JSONL parity executable, which may be a mode of the main binary |

### 5.3 Dependency direction

```text
statewright_common
  -> statewright_contracts
  -> statewright_core
  -> statewright_reasoning
  -> statewright_saa
  -> statewright_egcf
  -> statewright_application
  -> statewright CLI
```

Additional rules:

- provider interfaces are declared below provider implementations;
- deterministic reasoning does not depend on a provider implementation;
- SAA represents reasoning semantics but does not own live OIEC-SR runtime
  hypotheses or certificates;
- OIEC-SR exports a versioned `ReasoningOutcome` adapter for SAA/EGCF;
- EGCF depends on abstract transaction, reasoning, algorithm-store, and
  executor interfaces;
- core libraries never include CLI, application, provider implementation, or
  adapter headers; and
- no global service locator or mutable singleton is permitted.

### 5.4 Proposed repository layout

```text
CMakeLists.txt
CMakePresets.json
cmake/
contracts/
  schemas/
  fixtures/
  migrations/
resources/
  algorithms/v1/
  commands/v1/
  workflows/v1/
  benchmarks/reasoning/
Core/
  include/statewright/
    common/
    contracts/
    core/
    reasoning/
    saa/
    egcf/
    providers/
    application/
  src/
    common/
    contracts/
    core/
    reasoning/
    saa/
    egcf/
    providers/
    application/
Apps/
  statewright/
Tests/
  unit/
  contracts/
  parity/
  persistence/
  fault/
  integration/
  properties/
  benchmarks/
Tools/
  fixture_export/
  parity_runner/
  ledger_verify/
  projection_rebuild/
third_party/
  manifest.lock
```

The source resource paths remain language-neutral. They may be copied into an
installation layout, but their identity must not be changed merely to match the
C++ directory structure.

## 6. Core Data and Persistence Model

### 6.1 Common identity

All immutable objects use typed content identities:

```text
<object-type>:sha256:<digest>
```

Stable names and semantic versions identify definitions. Exact content IDs
identify executable instances, plans, approvals, evidence, candidates,
reasoning certificates, algorithm IR, qualifications, and results.

Canonical serialization must define:

- UTF-8 validation and normalization policy;
- object-key ordering;
- integer and exact-rational encoding;
- decimal encoding and precision;
- absent versus null fields;
- list ordering rules;
- set canonicalization rules;
- timestamp format;
- path separator and canonical-path rules; and
- rejection of non-finite floating-point values.

### 6.2 Two distinct hypothesis owners

StateWright must preserve two non-interchangeable hypothesis domains:

1. **Operational hypotheses:** evidence-linked beliefs used by the governed
   control loop, collision handling, and progress tracking.
2. **Reasoning hypotheses:** bounded competing explanations and candidate paths
   used inside OIEC-SR.

They may share low-level score and evidence-reference types. They must not share
an aggregate owner, lifecycle, normalization rule, signature, or persistence
record unless a versioned contract explicitly proves equivalence.

### 6.3 Canonical StateWright storage

```text
.statewright/
  lock
  ledger/
    events.jsonl
  objects/
    <sha256-prefix>/<digest>
  evidence/
    <evidence-id>.json
  transactions/
    <transaction-id>/
      transaction.json
      candidate/
      original/
  reasoning/
    certificates/
    benchmarks/
  egcf/
    objects/
    artifacts/
    executions/
  projections/
    runtime.json
    egcf.sqlite3
  recovery/
```

Authority rules:

- the event ledger and immutable content-addressed objects are canonical;
- runtime JSON and SQLite are rebuildable projections;
- candidate and original transaction bytes are retained until the applicable
  finalization and retention policy permits removal;
- every projection stores the source ledger position and chain hash;
- projection mismatch triggers rebuild or fail-closed behavior, never silent
  acceptance; and
- writes use locking, temporary files, flush, `fsync`, atomic rename, parent
  directory synchronization, and restart recovery appropriate to Linux.

## 7. OIEC-SR Design

### 7.1 Reasoning flow

```text
Reasoning problem
  -> bounded budget allocation
  -> competing hypothesis generation
  -> bounded path construction
  -> topology validation
  -> independent deterministic verification
  -> adversarial falsification
  -> contradiction analysis
  -> deterministic scoring and diversity filtering
  -> bounded synthesis
  -> synthesis verification
  -> reasoning certificate
  -> optional EGCF command or evidence request proposal
```

The reasoning certificate is an explanation and selection record. It is not an
approval, evidence gate, algorithm qualification, or mutation authority.

### 7.2 Required durable records

- `ReasoningProblem`;
- `OperationalHypothesis`, `OperationalHypothesisSet`, and evidence links;
- `ReasoningHypothesis`, `ReasoningHypothesisSet`, and update records;
- `ReasoningNode`, `ReasoningEdge`, and `ReasoningTopology`;
- `ReasoningStep` and `ReasoningPath`;
- `VerifierReport` and deterministic adapter results;
- `FalsifierReport` and counterexamples;
- `ContradictionRecord`;
- diversity and score configurations;
- `CandidateSet`;
- `SynthesisResult`;
- `ReasoningContext`;
- `ReasoningBudget`;
- `ReasoningMetrics`;
- `ReasoningOperationChoice`; and
- `ReasoningCertificate`.

### 7.3 Deterministic kernels

The following are provider-independent and must have pure or explicitly
state-bound APIs:

- score range checking and exact normalization;
- hypothesis identity and order-independent signatures;
- evidence-bound updates;
- topology identity and validation;
- inference-mode validation;
- budget admission and consumption;
- path scoring and ranking;
- contradiction creation and resolution state;
- diversity filtering;
- verifier adapter execution;
- falsifier record validation;
- synthesis compatibility checks;
- reasoning-certificate construction and integrity; and
- benchmark eligibility and qualification decisions.

### 7.4 Provider boundary

Providers receive bounded, versioned requests containing only declared task,
context, evidence references, limits, and output schemas. Provider responses
remain untrusted candidate data until parsed and deterministically validated.

Provider identity includes exact provider type, model tag, model digest when
available, context configuration, prompt/protocol version, sampling settings,
and implementation build identity.

## 8. Searchable Algebra of Algorithms Design

### 8.1 Canonical algorithm representation

The canonical IR owns:

- typed inputs, outputs, parameters, states, and dimensions;
- algorithm nodes and control/data edges;
- operand references and exact object links;
- preconditions, postconditions, invariants, and failure modes;
- normalization and denormalization contracts;
- dynamics, state transition, and control structure;
- semantic concepts and units;
- provenance and evidence;
- implementation digests and qualification references; and
- canonical behavior and structure signatures.

An executable implementation is referenced by the IR and algorithm definition.
It is not embedded as an arbitrary callback in a command or catalog object.

### 8.2 Canonical algebra layers

Implementation proceeds through these semantic layers:

1. **Primitives and IR:** operands, ports, state, nodes, edges, graphs,
   canonicalization, validation, and typed references.
2. **Normalization:** numeric bounds, role bindings, time normalization,
   provenance, strength, and exact signatures.
3. **Linear and dynamic structure:** dynamics, MIMO coupling, controllability
   metadata, representative inputs, representative zeros, and representative
   forms.
4. **Semantic structure:** concepts, units, ontology, meaning alignment,
   semantic revision, and semantic resolution.
5. **Nonlinear structure:** local/global behavior, geometry, lifts, transforms,
   control, stability, Lie structure, jets, evidence, and remainder bounds.
6. **Reasoning structure:** reasoning semantics, equivalence, composition,
   fit, outcomes, and certificate-derived evidence.
7. **Improvement structure:** transfer, adaptation, lineage, experiments,
   aggregation, failure algebra, knowledge integrity, promotion governance,
   OIEC-Bench admission, and scheduling.

### 8.3 Canonical store and query projection

Algorithm definitions, IR objects, relations, qualifications, outcomes,
failures, and supersedence records are immutable content-addressed objects.
SQLite provides a rebuildable projection containing:

- exact ID and version indexes;
- object type and lifecycle indexes;
- domain, concept, unit, capability, and risk indexes;
- input/output/state/shape indexes;
- structure and behavior signatures;
- qualification context, expiry, and implementation digest indexes;
- relation graph edges;
- evidence and outcome summaries;
- failure and counterexample indexes; and
- FTS5 lexical fields for names, descriptions, concepts, assumptions, and
  explanations.

The first release does not require embeddings. An optional embedding or model
adapter may propose candidate concepts, but deterministic records and ranking
remain authoritative.

### 8.4 Deterministic retrieval pipeline

```text
Problem requirements
  -> exact schema and authority validation
  -> capability and risk filtering
  -> lifecycle and qualification filtering
  -> lexical and semantic candidate retrieval
  -> structural signature comparison
  -> mathematical fit assessment
  -> reasoning fit and outcome assessment
  -> evidence, freshness, and failure penalties
  -> deterministic score and tie-break
  -> candidate set, exclusions, and retrieval explanation
```

No stage may silently drop a candidate. Exclusion records must state the exact
rule, missing capability, failed invariant, expired qualification, semantic
mismatch, mathematical mismatch, evidence gap, or tie-break result.

## 9. EGCF Design

### 9.1 Command flow

```text
Intent
  -> typed command invocation
  -> command context and authority intersection
  -> capability requirement union
  -> algorithm query
  -> deterministic searchable-algebra retrieval
  -> contextual qualification
  -> immutable selection decision
  -> workflow compilation
  -> simulation
  -> evidence and assurance assessment
  -> exact-plan approval
  -> governed transaction or executor action
  -> verification
  -> completion or rollback
  -> execution, outcome, failure, and learning records
```

### 9.2 Required EGCF records

- intent, command definition, and invocation;
- capability specification and grant;
- algorithm definition and qualification;
- selection decision;
- claim, evidence requirement, and evidence artifact;
- confidence assessment;
- invariant and decision;
- workflow node, workflow definition, and compiled workflow;
- execution plan and approval;
- execution and rollback;
- failure and collision;
- assurance case;
- artifact and supersedence; and
- adaptation, experiment, promotion, and improvement records needed by the
  searchable algebra.

### 9.3 Lifecycle

```text
DISCOVERED
  -> INTERPRETED
  -> MODELLED
  -> RESOLVED
  -> QUALIFIED
  -> COMPILED
  -> SIMULATED
  -> AWAITING_APPROVAL
  -> AUTHORIZED
  -> EXECUTING
  -> VERIFYING
  -> COMPLETED
```

Terminal alternatives are:

```text
REFUSED | FAILED | ROLLED_BACK | PARTIALLY_COMPENSATED | SUPERSEDED
```

Each lifecycle stage is recorded as completed, not required with a policy
reason, or blocked. A convenient CLI operation may compress stages but may not
erase them from the canonical record.

### 9.4 Integration with reasoning

OIEC-SR may provide:

- a structured problem interpretation;
- competing hypotheses;
- evidence requests;
- a verified reasoning certificate;
- a proposed command invocation;
- proposed algorithm requirements; and
- counterexamples or failure hypotheses.

EGCF independently validates the invocation, resolves capabilities, retrieves
and qualifies algorithms, and compiles the plan. A reasoning score cannot
replace algorithm qualification or human approval.

## 10. Dependencies

The initial dependency set is deliberately bounded:

- `nlohmann/json` for JSON values and serialization;
- SQLite3 with FTS5 for rebuildable projections and search;
- OpenSSL EVP for SHA-256;
- Boost.Multiprecision for arbitrary-precision integers and exact rational
  support;
- Catch2 v3 for tests;
- CMake/CTest for build and test dispatch; and
- optional exact-version llama.cpp support behind a provider build feature.

Every dependency requires a locked version or accepted system-version policy,
source URL, license, checksum where applicable, offline-build strategy, update
procedure, and compatibility test.

## 11. Migration and Compatibility Protocol

### 11.1 Frozen fixtures

SW0 exports accepted and rejected fixtures for:

- canonical JSON and Unicode;
- typed IDs and signatures;
- every scoped durable record and default;
- strict schema behavior;
- path normalization and escape rejection;
- authority and policy decisions;
- event-chain verification and redaction;
- transactions across preparation, interruption, apply, verification,
  finalization, and rollback;
- operational hypotheses and evidence updates;
- OIEC-SR hypotheses, topology, scoring, verification, falsification,
  contradiction, synthesis, certificates, and budgets;
- reasoning benchmark decisions;
- EGCF catalogs, capabilities, qualification, selection, compilation,
  simulation, approval, execution, rollback, assurance, and replay;
- canonical algorithm IR, normalization, dynamics, MIMO, representative forms,
  semantic and nonlinear relations;
- retrieval, transfer, adaptation, experiments, failure algebra, and promotion;
  and
- CLI help, exit codes, standard output, standard error, and JSON output.

### 11.2 Shadow protocol

A versioned JSONL protocol allows the Python oracle and C++ candidate to execute
pure operations against identical frozen inputs.

Each request binds:

- operation and schema version;
- source-fixture hash;
- input hash;
- expected contract version;
- deterministic configuration; and
- provider identity where applicable.

Each response binds:

- output hash;
- structured diagnostics;
- implementation build identity;
- resource usage; and
- timing as non-authoritative evidence.

Canonical outputs compare byte-for-byte. Semantic records compare by frozen
field and ordering rules. Rational results compare exactly. Floating adapters
compare using explicitly frozen tolerances.

### 11.3 Per-owner cutover rule

1. freeze Python behavior and fixtures;
2. implement C++ as a non-authoritative candidate;
3. run deterministic shadow parity;
4. resolve every mismatch or record a versioned intentional difference;
5. run fault, resource, and performance tests;
6. enable an explicit controlled-cutover flag;
7. verify post-cutover state and artifacts;
8. exercise rollback to the Python owner;
9. re-enable C++ and complete an observation window; and
10. retire the Python owner only after explicit approval.

## 12. Implementation Phases

## Phase SW0 — Freeze Source and Requirements

### Work

- Decide whether the oracle is the clean commit, an approved subset of current
  local changes, or a new reviewed source commit.
- Copy or archive the exact selected source state before broad fixture export.
- Generate a tracked-file manifest with path, mode, size, SHA-256, and owner.
- Inventory all scoped modules, symbols, tests, schemas, catalogs, workflows,
  grammars, benchmark inputs, and persistent schemas.
- Build a requirement-to-source-to-test matrix.
- Separate canonical source from generated reports, caches, projections, and
  runtime state.
- Capture the exact 35-file/342-test discovery set and then replace those
  provisional counts with frozen authoritative counts.
- Freeze accepted and rejected contract fixtures.
- Record known defects and decide whether each is preserved, fixed in the
  Python oracle first, or intentionally corrected through a versioned C++
  contract.

### Gate SW0

- One immutable source identity is selected.
- Every scoped requirement has a canonical source owner and at least one
  planned verification method.
- No dirty live file or generated report is silently treated as oracle source.
- Fixture and resource manifests verify byte-for-byte.

## Phase SW1 — Establish the C++ Workspace and Contracts

### Work

- Create root CMake, presets, dependency lock, warnings, sanitizers, coverage,
  formatting, and CTest structure.
- Implement `statewright_common` and `statewright_contracts`.
- Implement canonical JSON, UTF-8 validation, SHA-256, typed IDs, exact
  rationals, bounded integers, timestamps, stable errors, and build identity.
- Add schema-version dispatch and strict unknown-field behavior.
- Add the migration shadow executable with no mutation authority.
- Import language-neutral resources without changing their bytes.

### Gate SW1

- Clean developer, sanitizer, and release builds pass.
- Every foundation contract fixture round-trips through Python and C++.
- Canonical hashes and typed IDs match exactly.
- Resource manifests remain unchanged.

## Phase SW2 — Governed Runtime, Persistence, and Transactions

### Source owners

- `ourd/models.py`;
- `ourd/authority.py`;
- `ourd/workspace.py`;
- `ourd/policy.py`;
- `ourd/persistence.py`;
- `ourd/transactions.py`;
- `ourd/cfel.py`; and
- applicable constants and error owners.

### Work

- Port path canonicalization, scope matching, traversal rejection, symlink
  escape rejection, internal-state protection, snapshots, and environment
  sanitization.
- Port authority, capability, risk, approval, and policy records.
- Implement immutable objects, hash-chained events, state projection rebuild,
  redaction, locking, and recovery.
- Port candidate preparation, exact original/candidate identity, source-drift
  checks, atomic multi-file apply, post-write verification, finalization,
  discard, rollback, and restart recovery.
- Port collision identities and no-blind-retry enforcement.
- Implement exact-argv local process execution as a governed executor.

### Gate SW2

- Existing frozen state and transaction fixtures load without identity drift.
- Path and symlink attacks fail closed.
- Fault injection around every persistence boundary preserves a valid ledger
  and recoverable transaction state.
- Rollback restores exact bytes and required file modes.
- No higher layer can write directly to the workspace.

## Phase SW3 — OIEC Control and Operational Hypotheses

### Source owners

- `ourd/oiec.py`;
- `ourd/loop_control.py`;
- `ourd/hypotheses.py`; and
- the operational hypothesis records in the runtime model.

### Work

- Port fixed-point scores, entropy, utility, quantization, evidence projections,
  attempt keys, progress certificates, collision severity, convergence, and
  transition preparation required by the scoped runtime.
- Port bounded operational hypothesis creation, evidence linkage, updates,
  falsification retention, signatures, and public projections.
- Port repeated-step and cycle prevention.
- Exclude general corpus summarization unless a frozen reasoning fixture proves
  it is required for OIEC-SR parity.

### Gate SW3

- Operational hypothesis identities, scores, and updates match exactly.
- Failed actions cannot be retried through cosmetic changes.
- Non-progress and cycles are rejected with stable records.
- Operational hypotheses remain distinct from OIEC-SR hypotheses.

## Phase SW4 — OIEC-SR Durable Model and Deterministic Kernels

### Source owners

- `ourd/reasoning/models.py`;
- `ourd/reasoning/hypotheses.py`;
- `ourd/reasoning/topology.py`;
- `ourd/reasoning/budget.py`;
- `ourd/reasoning/scoring.py`;
- `ourd/reasoning/diversity.py`;
- `ourd/reasoning/contradictions.py`;
- deterministic portions of verifier, falsifier, synthesis, causal, context,
  qualification, and adapter modules.

### Work

- Port every scoped durable reasoning record with exact defaults and
  invariants.
- Implement exact hypothesis normalization and update provenance.
- Implement topology construction, identity, inference modes, grounding, and
  validation.
- Implement deterministic verifier adapters, contradiction lifecycle,
  falsifier validation, scoring, ranking, diversity, synthesis compatibility,
  and certificate integrity.
- Preserve exact rational and controlled decimal behavior.
- Add property tests for ordering, signature stability, budget monotonicity,
  boundedness, and rejected malformed structures.

### Gate SW4

- All deterministic reasoning fixtures have exact decision and identity parity.
- Rational outputs match exactly and decimal adapters meet frozen tolerances.
- Malformed topology, unsupported inference modes, missing evidence, and budget
  overflow fail closed.
- No provider is required to run deterministic reasoning tests.

## Phase SW5 — OIEC-SR Search, Providers, Benchmarks, and Qualification

### Source owners

- remaining `ourd/reasoning/` modules;
- scoped provider interfaces; and
- `benchmarks/reasoning/` resources and manifests.

### Work

- Port bounded candidate generation and provider response parsing.
- Port bounded multi-path search, verification/falsification orchestration,
  synthesis, certificate creation, ablation, benchmark execution, source-bound
  result merging, and qualification.
- Implement the subprocess provider protocol.
- Add optional exact-version llama.cpp integration without linking model
  authority into the reasoning kernel.
- Preserve benchmark task, manifest, model, run, checksum, and eligibility
  identities.

### Gate SW5

- Frozen provider-free benchmark decisions match exactly.
- Provider-backed runs bind exact model and runtime identity.
- Provider output cannot self-verify, self-qualify, or create approval.
- Benchmark eligibility and qualification decisions match the oracle on all
  checked-in runs.
- Resource limits terminate safely and produce complete failure records.

## Phase SW6 — EGCF Records, Registries, Ledger, and Search Projection

### Source owners

- `ourd/egcf/models.py`;
- `ourd/egcf/ids.py`;
- registry, schema, store, evidence, decisions, lifecycle, and qualification
  modules; and
- EGCF schemas and catalogs.

### Work

- Port all scoped EGCF durable records and lifecycle transitions.
- Port strict command and algorithm registries.
- Implement immutable canonical EGCF objects over the StateWright ledger.
- Implement SQLite schema creation, migrations, rebuild, integrity checking,
  FTS5 fields, exact indexes, and projection checkpoints.
- Import command, algorithm, workflow, and schema resources with manifest
  verification.
- Implement supersedence without mutation of prior records.

### Gate SW6

- Every EGCF contract fixture has exact ID and accepted/rejected parity.
- SQLite can be deleted and rebuilt from canonical objects and events.
- Corrupt or stale projections are rejected or rebuilt deterministically.
- Registry resolution never uses floating `latest` identities for execution.

## Phase SW7 — Canonical Searchable Algorithm Algebra

### Source owners

- algebra primitives, models, IR, graph, normalization, dynamics, MIMO,
  representative, representative-zero, representative-form, semantic, units,
  ontology, and foundational reasoning-algebra modules.

### Work

- Port typed algebra records and canonicalization.
- Port graph and structure validation.
- Port normalization and denormalization with provenance.
- Port exact rational linear dynamics and MIMO behavior.
- Port representative inputs, zeros, and canonical forms.
- Port semantic concepts, units, ontology, alignment, revision, and resolution.
- Port reasoning semantic, fit, equivalence, composition, and outcome records.
- Implement canonical admission into the immutable algorithm store.
- Implement deterministic lexical, semantic, structural, mathematical, and
  reasoning-fit retrieval stages.

### Gate SW7

- Every foundational SAA fixture has exact identity and decision parity.
- Canonical admission is order-stable and idempotent.
- Equivalent forms resolve consistently without erasing provenance.
- Retrieval returns a complete candidate/exclusion explanation.
- SQLite projections reproduce canonical-store answers.

## Phase SW8 — Advanced Algebra, Transfer, Adaptation, and Improvement

### Source owners

- nonlinear, transfer, adaptation, lineage, experiment, aggregation, failure,
  knowledge-integrity, promotion, benchmark-gate, intelligence-loop, and
  improvement-scheduling modules.

### Work

- Port nonlinear local/global/control/stability/Lie/jet/lift/transform/evidence
  and remainder contracts.
- Port unified retrieval and retrieval explanations.
- Port cross-domain transfer assessment and gap classification.
- Port bounded one-dimension and multi-step adaptation with frozen invariants.
- Port lineage, experiments, aggregation, and promotion gates.
- Port canonical failure algebra and reasoning/algorithm outcome relations.
- Port OIEC-Bench admission, knowledge integrity, closed improvement records,
  and deterministic scheduling.

### Gate SW8

- All advanced SAA fixtures have exact decisions and stable identities.
- Adaptation cannot bypass invariant, qualification, evidence, experiment, or
  approval requirements.
- Failed algorithms remain queryable and influence deterministic retrieval.
- Promotion remains blocked without exact benchmark and approval evidence.

## Phase SW9 — EGCF Compiler, Command Fabric, and Governed Execution

### Source owners

- EGCF catalog, compiler, handlers, engine, context, approval, assurance,
  simulation, adapters, brain feed, repository feed, CLI, and integration
  modules.

### Work

- Port universal command modifiers through one canonical `CommandContext`.
- Port capability requirement union and authority intersection.
- Port deterministic algorithm selection through the StateWright SAA service.
- Port immutable workflow compilation and plan hashing.
- Port simulation, assurance, evidence gates, and exact-plan approval.
- Compile executable leaves into StateWright actions and transactions.
- Port verification, rollback, execution records, failure learning, replay,
  brain feed, and repository feed required by the frozen scope.
- Integrate OIEC-SR as a bounded proposal and evidence-request service.

### Gate SW9

- The complete typed-command lifecycle passes end to end.
- Composite commands cannot launder authority or capability requirements.
- No EGCF adapter exposes a second filesystem writer.
- Forged approval data is insufficient without the canonical approved object.
- Simulation and execution bind the same immutable plan.
- Rollback and post-action verification pass for the first mutating vertical
  slice and every subsequently admitted mutation class.

## Phase SW10 — Native Application, CLI, Cutover, and Release Qualification

### Work

- Implement the `statewright` CLI and stable JSON output.
- Support inspect, reason, hypothesis, algorithm, retrieve, explain, command,
  compile, simulate, approve, execute, verify, rollback, replay, ledger-verify,
  projection-rebuild, benchmark, and qualification operations required by the
  frozen matrix.
- Package schemas, catalogs, workflows, algorithm resources, and benchmark
  fixtures with exact manifests.
- Run unit, contract, parity, persistence, property, fault, integration,
  benchmark, sanitizer, performance, package, and release suites.
- Execute controlled cutover and rollback separately for core, reasoning, SAA,
  and EGCF ownership.
- Produce immutable qualification evidence and a residual-risk report.

### Gate SW10

- The scoped executable runs without a Python runtime.
- Every scoped public contract is preserved or has an approved versioned
  migration.
- Every frozen source owner is retired or retained only as an explicit
  compatibility adapter.
- Existing persistent records load or fail with a deliberate migration
  diagnostic.
- All search, reasoning, qualification, approval, transaction, verification,
  and rollback decisions meet the frozen comparison rules.
- A clean install into an empty prefix passes all supported CLI smoke tests.
- Release hashes, dependency identities, test results, migration evidence,
  rollback evidence, residual risks, and human approval are recorded.

## 13. First Integrated Delivery Slice

The first useful delivery is not the whole plan. It is a non-authoritative
vertical slice proving that the architecture composes correctly:

1. load frozen command, algorithm, and schema resources;
2. admit one exact canonical algorithm into the immutable store;
3. rebuild the SQLite search projection;
4. accept one bounded reasoning problem;
5. construct and deterministically score multiple fixture hypotheses;
6. produce a verified reasoning certificate without provider authority;
7. convert the certificate into a typed EGCF command proposal;
8. retrieve the admitted algorithm with a complete selection explanation;
9. compile and simulate one read-only workflow;
10. compile one two-file candidate transaction without applying it;
11. bind an external human approval to the exact plan hash;
12. apply through the sole StateWright transaction boundary;
13. verify the resulting hashes and bounded command evidence;
14. finalize on success and exercise exact rollback in a separate run; and
15. compare every authoritative record with the frozen Python oracle.

This slice remains non-authoritative until the applicable parity, fault, and
rollback gates pass.

## 14. Validation Strategy

### 14.1 Test layers

1. **Unit:** pure records, parsers, validators, exact arithmetic, graph and
   scoring kernels.
2. **Contract:** schemas, canonical JSON, IDs, signatures, errors, resources,
   protocols, and CLI projections.
3. **Parity:** Python oracle versus C++ candidate on immutable fixtures.
4. **Property:** ordering stability, idempotence, bounds, monotonicity,
   round-trips, graph invariants, and retrieval determinism.
5. **Persistence:** ledger chains, immutable objects, migrations, projection
   rebuilds, transaction restart, and retention.
6. **Fault:** malformed data, I/O failures, crashes, timeouts, cancellation,
   dependency absence, source drift, hash mismatch, stale approval, corrupt
   projection, and partial provider output.
7. **Integration:** reasoning, retrieval, command compilation, simulation,
   governed mutation, verification, rollback, and failure learning.
8. **Benchmark:** exact manifests, source identity, checksums, decisions,
   eligibility, ablations, latency, and resource use.
9. **Security:** traversal, symlink escape, argument injection, approval
   forgery, capability laundering, malformed object references, and ledger
   tampering.
10. **Package:** clean install, resource discovery, optional dependency
    behavior, and execution without repository-local imports.
11. **Release:** frozen-source regeneration, full requirement matrix, rollback,
    observation window, evidence manifest, and human approval.

### 14.2 Comparison rules

- byte equality for canonical JSON, hashes, IDs, signatures, event records,
  immutable objects, schemas, catalogs, manifests, and checksums;
- exact structured equality where presentation ordering is declared
  non-semantic;
- exact rational equality;
- frozen tolerance for intentional floating-point numerical adapters;
- exact lifecycle-state equivalence;
- exact accepted/rejected equivalence for validators and policies;
- complete candidate and exclusion equivalence for deterministic retrieval;
- exact benchmark eligibility decisions; and
- versioned difference receipts for intentional corrections.

### 14.3 Evidence retained per phase

- frozen source manifest and commit/archive identity;
- requirement matrix;
- build identity and dependency lock;
- test inventory and results;
- fixture and resource manifests;
- parity mismatches and dispositions;
- fault and security results;
- benchmark and resource measurements;
- migration and rollback results;
- residual risks; and
- human approval status.

## 15. Principal Risks and Countermeasures

| Risk | Countermeasure |
| --- | --- |
| A minimal project becomes another full OIEC rewrite | Enforce the explicit product boundary and completion matrix |
| Python and C++ both become authoritative | Per-owner cutover flags, one canonical owner, mandatory retirement gate |
| Operational and reasoning hypotheses are conflated | Separate records, owners, signatures, persistence, and adapters |
| SAA reasoning relations duplicate OIEC-SR runtime state | Export immutable reasoning outcomes; never share mutable ownership |
| Search ranking becomes opaque or model-controlled | Deterministic staged retrieval with complete score and exclusion records |
| SQLite becomes accidental authority | Rebuild tests from immutable ledger and object store |
| Floating-point translation changes decisions | Exact rationals and frozen decimal/tolerance policies |
| Algebra port reproduces import cycles | Enforced target dependency graph and interface-level composition |
| Provider responses smuggle authority | Strict schemas and deterministic revalidation of every proposal |
| EGCF introduces a second writer | Compile only into StateWright actions and transactions |
| Dirty source contaminates fixtures | SW0 immutable source freeze before generation |
| Broad green tests hide missing scope | Requirement-by-requirement evidence and complete frozen inventory |
| Generated reports become migration inputs | Source/evidence separation and manifest classification |
| Rollback works only in fixtures | Restart, crash, and real filesystem fault tests before cutover |
| Advanced adaptation self-promotes | Qualification, experiment, benchmark, evidence, and human approval gates |

## 16. Completion Definition

The StateWright refactor is complete only when all of the following are true:

- the exact scoped source inventory and requirement matrix are closed;
- governed runtime, operational hypotheses, OIEC-SR, EGCF, and searchable
  algorithm algebra have authoritative C++ owners;
- all scoped Python owners are retired or reduced to explicitly temporary
  compatibility adapters;
- canonical records, resources, hashes, IDs, exact arithmetic, decisions, and
  lifecycle transitions satisfy the frozen comparison rules;
- the immutable ledger and object store are authoritative and SQLite rebuilds
  exactly from them;
- deterministic retrieval produces complete candidate, exclusion, score, and
  tie-break explanations;
- models remain bounded proposal sources and cannot approve, qualify, promote,
  mutate, or declare release success;
- every mutating path uses the sole StateWright transaction service and has
  verified rollback;
- reasoning and benchmark qualification gates pass against the frozen source;
- the full scoped validation and package suites pass from a clean build;
- the final evidence bundle binds source, build, dependencies, resources,
  tests, migrations, rollback, residual risks, and release artifacts; and
- an authorized human explicitly approves the cutover and release.

Anything less is an implementation checkpoint, not completion of this plan.
