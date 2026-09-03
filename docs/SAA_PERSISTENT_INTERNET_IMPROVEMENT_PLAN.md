# SAA Persistent Internet Improvement System Implementation Plan

**Plan date:** 2026-09-02

**Implementation-qualified date:** 2026-09-03

**Target:** StateWright C++20

**Status:** Implemented and fixture-scope release-qualified

**Promotion authority:** Deterministic autonomous policy; no per-candidate human
approval

## 1. Executive Decision

StateWright will add a persistent, bounded internet acquisition and autonomous
improvement system for the Searchable Algebra of Algorithms (SAA).

The internet system is an evidence and candidate source. It is not a new
canonical algorithm owner, an execution authority, a model-training system, or
an alternative EGCF command engine. Exact source snapshots, deterministic
qualification, immutable decision records, probationary admission, automatic
promotion, and automatic demotion compose through the existing StateWright SAA
and EGCF owners.

No human review, signature, confirmation, or approval is required for normal
candidate promotion. Promotion authority is granted only by an immutable,
versioned `AutonomousPromotionPolicy` evaluated by deterministic native code.
OIEC-SR and external model providers may propose interpretations, hypotheses,
falsifiers, mappings, and experiments, but cannot satisfy authoritative gates by
assertion or confidence.

Automatic SAA admission does not grant EGCF command execution authority. A
canonical algorithm remains data and qualified knowledge until a separately
authorized command plan selects and executes it.

## 2. Existing Canonical Owners

Implementation must extend rather than replace these owners:

- `BrainFeedProcessor` owns batch ingestion, disposition, staging, and
  quarantine.
- `CanonicalAlgorithmStore` owns canonical mathematical algorithm identities,
  source provenance, relations, and retrieval projection.
- `KnowledgeGovernanceStore` owns failure patterns, benchmark gates, integrity
  snapshots, opportunities, and schedules.
- `ImprovementLoopStore` owns immutable evolution, experiment, and improvement
  decisions.
- `IntelligenceImprovementDecision` owns the deterministic improvement-loop
  phase decision.
- `CanonicalPromotionGovernanceAssessment` owns promotion-gate composition.
- OIEC-SR owns bounded advisory reasoning and hypothesis machinery.
- EGCF owns orchestration, persistence registration, command lifecycle, and
  operational policy.

The implementation must not create an `InternetAlgorithmStore`, an
internet-specific canonical namespace, or a second promotion function.

## 3. Scope

### 3.1 In scope

- persistent internet watches and bounded polling schedules;
- unauthenticated HTTP and HTTPS acquisition;
- exact immutable response snapshots;
- redirect, DNS, TLS, MIME, size, decompression, and timing controls;
- robots and license-policy evidence;
- deterministic content extraction;
- source claims, code fragments, mathematical expressions, tables, metadata,
  and benchmark observations;
- conversion into ordinary `BrainFeedItem` batches;
- existing-algorithm retrieval, equivalence, relation, and failure matching;
- OIEC-SR advisory interpretation, hypothesis, contradiction, and falsifier
  generation;
- construction of staged SAA algorithm and adaptation candidates;
- controlled experiments and independent evidence aggregation;
- deterministic benchmark and knowledge-integrity qualification;
- autonomous promotion policy evaluation;
- probationary canonical admission;
- bounded canary retrieval and longitudinal monitoring;
- automatic promotion, demotion, supersedence, and previous-preference
  restoration;
- crash-safe scheduling, leases, replay, and projection rebuild;
- stable JSON CLI operations;
- fixture, contract, fault, security, persistence, sanitizer, package, and
  release tests.

### 3.2 Explicitly out of scope for the first release

- authenticated browsing;
- cookies, stored browser profiles, or ambient credentials;
- JavaScript execution or a general browser engine;
- CAPTCHA solving;
- unbounded crawling or general-purpose search-engine indexing;
- arbitrary downloaded binary or source-code execution;
- compiling internet source code as a qualification shortcut;
- model-weight training or fine-tuning;
- model confidence as evidence or authority;
- automatic EGCF C3/C5 execution authority;
- modification of the frozen EGCF v1 schemas;
- macOS or Windows qualification;
- distributed multi-host scheduling.

## 4. Non-Negotiable Invariants

1. **One canonical owner.** `CanonicalAlgorithmStore` remains the sole canonical
   owner of mathematical algorithm knowledge.
2. **Internet content is hostile data.** No text, markup, code, metadata, model
   output, or embedded instruction can alter policy or invoke a tool.
3. **Freeze before interpretation.** Extraction and reasoning operate only on
   immutable source snapshots, never a live response stream.
4. **Fetch and content identity are distinct.** Repeated retrieval of identical
   bytes creates new fetch receipts but reuses one content-addressed snapshot.
5. **Derived projections are not authoritative.** SQLite projections rebuild
   from immutable files and hash-chained EGCF records.
6. **No model authority.** OIEC-SR providers can propose but cannot pass source,
   semantic, mathematical, benchmark, integrity, promotion, or demotion gates.
7. **No human approval gate.** Normal promotion cannot pause for or depend on a
   person, signature, UI confirmation, or manually authored approval object.
8. **Policy is immutable and explicit.** Every autonomous decision binds one
   exact `AutonomousPromotionPolicy` ID and version.
9. **Hard gates are conjunctive.** A weighted score cannot compensate for a
   failed mandatory predicate.
10. **Admission is not execution.** Canonical SAA status grants no command
    capability, filesystem authority, process authority, or network authority.
11. **Probation precedes full canonical preference.** New internet-derived
    knowledge must pass bounded shadow or canary evaluation.
12. **Demotion is automatic and append-only.** A failed admission is superseded
    or de-preferred through a new record; historical evidence is never rewritten.
13. **Existing knowledge is searched first.** A candidate cannot be treated as
    novel before exact, semantic, mathematical, transfer, relation, and failure
    matching completes.
14. **Downloaded code is never executed.** Qualification runs internal SAA IR,
    native deterministic adapters, or checked-in fixtures only.
15. **Source diversity is measured.** Multiple pages controlled by one source
    group do not count as independent evidence.
16. **Freshness cannot rewrite history.** Updated content creates a new snapshot
    and supersedence relation.
17. **Every background action is replayable.** Watches, due jobs, leases,
    attempts, dispositions, decisions, and terminal outcomes are durable.
18. **Final validation uses a frozen generation.** Persistent acquisition writers
    are stopped or isolated while release evidence is generated.
19. **Limits fail closed.** Redirect, size, time, parser, schedule, evidence,
    and experiment limits produce terminal bounded records, not silent truncation.
20. **No ambient network in tests.** Automated tests use deterministic local
    fixtures unless a separately labeled live-network qualification is invoked.

## 5. Target Architecture

### 5.1 Dependency direction

```text
statewright_common
    -> statewright_contracts
        -> statewright_core
            -> statewright_sources
                -> statewright_egcf

statewright_contracts
    -> statewright_providers
        -> statewright_reasoning

statewright_contracts
    -> statewright_saa
        -> statewright_egcf

statewright_reasoning
    -> statewright_egcf

statewright_egcf
    -> statewright_application
```

`statewright_sources` owns bounded acquisition contracts and native HTTP I/O. It
must not depend on SAA, reasoning, EGCF, or application code. EGCF composes source
snapshots into brain-feed and SAA workflows.

### 5.2 Proposed source layout

```text
Core/include/statewright/sources/
  fetch.hpp
  http_provider.hpp
  policy.hpp
  records.hpp
  scheduler.hpp
  snapshot.hpp

Core/src/sources/
  fetch.cpp
  curl_http_provider.cpp
  policy.cpp
  records.cpp
  scheduler.cpp
  snapshot.cpp

Core/include/statewright/egcf/
  autonomous_promotion.hpp
  internet_feed.hpp
  internet_improvement_store.hpp

Core/src/egcf/
  autonomous_promotion.cpp
  internet_feed.cpp
  internet_improvement_store.cpp

Core/include/statewright/saa/
  autonomous_promotion_policy.hpp
  probation.hpp

Core/src/saa/
  autonomous_promotion_policy.cpp
  probation.cpp

resources/schemas/statewright-v1/
  internet-improvement-extension.schema.json

resources/policies/internet/
  default-source-policy-v1.json
  default-promotion-policy-v1.json

Tests/sources/
Tests/egcf/
Tests/saa/
Tests/fixtures/internet/
```

### 5.3 Principal components

#### `HttpFetchProvider`

A replaceable interface accepting a fully validated `FetchRequest` and returning
a bounded `FetchResponse`. The first native implementation uses libcurl with
exact dependency identity recorded in `third_party/manifest.lock.json`.

The provider performs network I/O only. It cannot parse SAA candidates, write
canonical knowledge, schedule follow-up work, or invoke OIEC-SR.

#### `InternetImprovementStore`

The immutable authoritative owner for internet watches, fetch receipts, source
snapshots, policy assessments, extraction receipts, candidates, probation
observations, and autonomous decisions. Its SQLite projection is derived.

#### `InternetFeedCoordinator`

Converts verified source snapshots into deterministic `BrainFeedItem` batches.
It reuses `BrainFeedProcessor` for staging, quarantine, duplicate handling,
evidence registration, and semantic routing.

#### `AutonomousPromotionController`

An EGCF coordinator that gathers authoritative records from the canonical,
governance, improvement, evidence, and internet stores; invokes pure SAA policy
evaluation; persists the decision; performs probationary admission; and schedules
post-admission verification.

It contains no alternative mathematical identity or semantic proof logic.

## 6. Persistent Record Model

All new records use the additive StateWright extension schema. Each record has a
typed content-addressed ID, schema version, producer version, exact dependencies,
and canonical JSON signature.

### 6.1 `InternetWatch`

Required fields:

- watch ID and superseded watch ID;
- canonical URL;
- enabled status;
- polling interval and deterministic jitter parameters;
- source-policy ID;
- accepted MIME types;
- domain and source-group identity;
- maximum redirects, bytes, and decompressed bytes;
- request timeout and total deadline;
- conditional-fetch state references;
- creation reason and schedule generation.

Changing a watch creates a new watch record and supersedes the old one.

### 6.2 `InternetFetchJob`

- watch ID;
- scheduled interval identity;
- expected watch generation;
- earliest start and deadline;
- retry number and retry ceiling;
- domain-budget allocation;
- deterministic job signature.

The unique job key is derived from watch ID and scheduled interval. Restarting a
worker cannot create duplicate logical jobs.

### 6.3 `InternetFetchLease`

- job ID;
- worker identity;
- lease acquisition time;
- lease expiry;
- predecessor lease, if any;
- terminal release, expiry, or abandonment state.

Leases are append-only events. A worker may act only while its latest lease is
current.

### 6.4 `InternetFetchReceipt`

- job and lease IDs;
- request method and canonical URL;
- resolved public addresses;
- redirect chain;
- HTTP status;
- selected response headers;
- TLS verification result;
- compressed and decompressed byte counts;
- timing measurements;
- provider and dependency versions;
- snapshot ID or bounded failure;
- conditional-fetch result;
- exact receipt signature.

### 6.5 `InternetSourceSnapshot`

- canonical URL;
- final URL;
- exact response-byte SHA-256;
- normalized content-type;
- immutable body path;
- selected stable metadata;
- source-group identity;
- content signature.

The snapshot identity excludes retrieval time so identical content can be reused.
Fetch receipts retain time and transport history and reference the snapshot.
First/latest receipt relationships are derived by the projection, avoiding a
cyclic content-addressed identity.

### 6.6 `InternetPolicyAssessment`

- snapshot and fetch receipt IDs;
- source-policy ID;
- public-address validation;
- redirect validation;
- robots result;
- license classification;
- MIME and encoding validation;
- authentication and credential-use result;
- content-size and decompression result;
- admissibility status and blocking reasons.

### 6.7 `InternetExtractionReceipt`

- snapshot ID;
- extractor set and versions;
- decoded text signature;
- extracted fragment IDs;
- rejected fragment summaries;
- deterministic parser diagnostics;
- truncation status;
- extraction signature.

### 6.8 `InternetAlgorithmCandidate`

- source fragment and snapshot IDs;
- proposed SAA IR;
- semantic input/output mappings;
- units and applicability;
- claimed invariants and termination properties;
- existing-algorithm retrieval receipt;
- exact, equivalent, related, transfer, and failure matches;
- OIEC-SR proposal and falsifier IDs;
- unresolved assumptions;
- candidate status and signature.

### 6.9 `AutonomousPromotionPolicy`

- policy version and domain scope;
- allowed candidate classes;
- prohibited execution or capability classes;
- minimum independent source groups;
- minimum independent experiment groups;
- required semantic and mathematical strengths;
- benchmark thresholds;
- invariant requirements;
- maximum contradiction and unresolved-falsifier counts;
- knowledge-integrity thresholds;
- probation windows and observation minima;
- canary retrieval limits;
- automatic demotion predicates;
- source-freshness requirements;
- exact policy signature.

Policy changes create a new policy ID. Existing decisions remain bound to their
original policy.

### 6.10 `AutonomousPromotionAssessment`

- candidate and baseline references;
- all gate input IDs;
- policy ID;
- one result per mandatory predicate;
- score components used only for scheduling or comparison;
- blocking reasons;
- resulting state;
- deterministic decision signature.

### 6.11 Probation records

`ProbationaryAdmission` records the initial bounded admission and previous
preferred canonical algorithm. `ProbationObservation` records each retrieval or
evaluation window. `AutomaticPromotionDecision` and `AutomaticDemotionDecision`
record the terminal transition.

### 6.12 Supersedence and retraction records

Source updates, retractions, semantic corrections, and failed canonical
admissions create explicit typed relations. No snapshot, candidate, or decision
is mutated in place.

## 7. Autonomous State Machine

```text
DISCOVERED
  -> FETCH_SCHEDULED
  -> FETCHED
  -> SNAPSHOT_VERIFIED
  -> EXTRACTED
  -> STAGED
  -> RETRIEVAL_COMPLETE
  -> VALIDATION_READY
  -> EXPERIMENT_QUALIFIED
  -> POLICY_QUALIFIED
  -> PROBATIONARY_CANONICAL
  -> CANONICAL
```

Terminal or side states:

```text
NOT_MODIFIED
DUPLICATE
EQUIVALENT_EXISTING
RELATED_EXISTING
TRANSFER_CANDIDATE
QUARANTINED
REJECTED
FETCH_FAILED
POLICY_BLOCKED
EXPERIMENT_FAILED
DEMOTED
SUPERSEDED
RETRACTED
```

Every transition is validated against the latest immutable predecessor record.
No transition is inferred from a mutable SQLite status column.

## 8. Source Acquisition Protocol

### 8.1 Request preparation

Before DNS or network access:

1. parse the URL strictly;
2. permit only HTTP or HTTPS;
3. reject credentials, fragments, local file forms, malformed ports, and
   unsupported encodings;
4. canonicalize host, port, path, and query according to a versioned rule;
5. resolve the applicable watch and source policy;
6. reserve domain and global budgets;
7. create the immutable fetch job and lease.

### 8.2 SSRF and address controls

- Resolve hostnames through the bounded provider.
- Reject loopback, private, link-local, multicast, unspecified, documentation,
  and otherwise prohibited address ranges.
- Pin the validated addresses used by the HTTP client.
- Repeat URL and address validation for every redirect.
- Reject redirects to different schemes or prohibited ports.
- Record every resolved and connected address.
- Fail closed on DNS changes that cannot be reconciled with the pinned set.

### 8.3 HTTP controls

- GET and HEAD only.
- No cookies, credential stores, proxy credentials, `.netrc`, or ambient auth.
- TLS peer and hostname verification enabled.
- Bounded redirect count.
- Bounded compressed and decompressed bytes.
- Bounded header bytes and count.
- Bounded connection, first-byte, idle, and total times.
- Explicit user agent and contact policy.
- Conditional requests use immutable prior receipt references.
- Unsupported encodings and MIME types fail closed.

### 8.4 Snapshot write protocol

1. Stream bytes into a temporary bounded capture.
2. Compute SHA-256 while receiving.
3. Verify final byte count and decompression limits.
4. Atomically place the immutable body under the content digest.
5. Register or reuse the snapshot record.
6. Register the fetch receipt.
7. Emit the policy-assessment job.

An interrupted capture cannot create a valid snapshot record.

## 9. Deterministic Extraction

Initial extractors are deterministic and versioned:

- plain text and normalized markup text;
- headings and section boundaries;
- code blocks with declared language;
- mathematical expressions;
- tables and benchmark rows;
- structured metadata such as JSON-LD when bounded and valid;
- citations and outbound source references;
- retraction, correction, and version markers.

HTML scripts, styles, hidden content, event handlers, and instruction-like text
are retained only as source data when needed; they are never executed.

Each extracted fragment binds exact byte ranges or deterministic normalized
selectors back to the immutable snapshot. Extraction cannot produce authoritative
facts without source references.

## 10. Brain Feed and Candidate Routing

The `InternetFeedCoordinator` converts extraction fragments into existing
brain-feed kinds. New internet-specific dispositions are limited to source-policy
outcomes; algorithm and semantic routing remains owned by `BrainFeedProcessor`.

Required routing sequence:

1. register source evidence;
2. detect duplicate content and duplicate fragments;
3. route source claims and semantic concepts;
4. stage algorithm fragments;
5. retrieve existing canonical knowledge;
6. attach exact match, equivalence, relation, transfer, and failure results;
7. quarantine incomplete or ambiguous candidates;
8. create validation-ready candidates only when required fields are complete.

Strict batches fail atomically at the disposition level: the receipt records all
results, but no candidate is silently omitted.

## 11. Existing-Knowledge-First Retrieval

Before novelty or adaptation classification, every candidate runs:

1. exact canonical-ID lookup;
2. structural and mathematical signature lookup;
3. semantic signature and ontology alignment;
4. representative-form equivalence;
5. reasoning-algorithm equivalence when applicable;
6. relation-neighbor search;
7. transfer and controlled-adaptation assessment;
8. known failure-pattern assessment;
9. freshness and qualification filtering;
10. deterministic explanation generation.

Possible results are `DUPLICATE`, `EQUIVALENT_EXISTING`, `RELATED_EXISTING`,
`TRANSFER_CANDIDATE`, `ADAPTATION_CANDIDATE`, or `NOVEL_CANDIDATE`.

Popularity, search rank, page count, model confidence, and source prose are not
novelty evidence.

## 12. OIEC-SR Integration

OIEC-SR receives only bounded snapshot fragments and typed existing-knowledge
context. It may produce:

- competing algorithm interpretations;
- semantic input/output hypotheses;
- applicability and unit hypotheses;
- claimed invariants and termination conditions;
- contradictions between sources;
- counterexamples and falsifiers;
- missing evidence requests;
- candidate experiment designs;
- explanations for transfer, adaptation, or rejection.

Every proposal records provider identity, model identity, request grammar,
snapshot IDs, input hash, output hash, parser version, uncertainty, and unresolved
assumptions.

Provider output cannot directly instantiate `POLICY_QUALIFIED`,
`PROBATIONARY_CANONICAL`, or `CANONICAL` state.

## 13. Experiment and Qualification Protocol

### 13.1 Candidate execution representation

Only internal SAA IR and checked-in deterministic adapters may execute during
qualification. Internet source code is treated as explanatory evidence and must
be translated into validated IR or a native supported primitive set.

### 13.2 Experiment design

Experiments bind:

- exact baseline and candidate references;
- context and dataset snapshot IDs;
- metrics and directions;
- minimum material effects;
- required invariants;
- evidence requirements;
- trial counts and deterministic seeds;
- independence-group rules;
- resource ceilings.

### 13.3 Required gates

- all required evidence is registered and successful;
- candidate and baseline observations use the same frozen context;
- required invariant results pass;
- trial minima pass;
- material improvement passes when required;
- independent evidence-group minima pass;
- benchmark gate passes;
- knowledge-integrity trajectory remains qualified;
- no known equivalent failure is retried without new evidence;
- no unresolved hard contradiction or successful falsifier remains.

## 14. Autonomous Promotion Policy

The pure SAA function returns `CanonicalPromotionGovernanceAssessment` extended
with policy identity, probation parameters, and demotion predicates.

The authoritative conjunctive decision is:

```text
promotion_allowed =
    source_policy_passed
    AND snapshot_integrity_passed
    AND source_independence_passed
    AND semantic_proof_verified
    AND mathematical_identity_verified
    AND existing_knowledge_search_complete
    AND experiment_qualified
    AND benchmark_gate_passed
    AND knowledge_integrity_qualified
    AND invariants_passed
    AND no_unresolved_hard_contradictions
    AND no_successful_falsifiers
    AND probation_plan_valid
    AND demotion_path_valid
    AND policy_scope_allows_candidate
```

Scheduling scores may order qualified work but cannot alter this predicate.

### 14.1 Policy bootstrap

The packaged default policy is a manifest-bound resource. Startup registers its
exact content-addressed identity. No mutable environment variable may weaken its
hard gates.

More permissive policies require a new resource, schema-valid policy record, and
normal package/release qualification. They do not require per-candidate approval.

## 15. Probation, Promotion, and Demotion

### 15.1 Probationary admission

An eligible candidate is admitted with status `PROBATIONARY_CANONICAL` and a
reference to the previous preferred canonical algorithm. Probationary knowledge
is searchable but cannot silently replace all qualified retrieval.

### 15.2 Canary retrieval

The policy defines bounded contexts in which the probationary candidate may be
selected. Every selection records baseline, candidate, query, explanation,
result, evidence, and integrity effects.

### 15.3 Automatic promotion

Full canonical preference occurs after all policy-defined observation windows,
minimum uses, benchmark repetitions, and integrity requirements pass. The
promotion decision is deterministic and persisted before the projection changes.

### 15.4 Automatic demotion

Demotion triggers include:

- source integrity failure or retraction;
- semantic contradiction above policy;
- successful falsifier;
- invariant failure;
- benchmark regression;
- retrieval precision regression;
- corrected-error recurrence;
- equivalent-failure retry regression;
- insufficient source or experiment independence;
- stale evidence beyond policy;
- reproduction failure;
- projection or record integrity failure.

Demotion removes preferred retrieval eligibility, restores the prior qualified
preference when valid, registers failure knowledge, and schedules re-evaluation.

## 16. Persistent Scheduler

The scheduler is record-driven and restart-safe.

### 16.1 Scheduling inputs

- enabled watches;
- source freshness deadlines;
- failed-job retry policies;
- probation observation schedules;
- revalidation and retraction checks;
- improvement opportunities;
- domain and global budgets.

### 16.2 Ordering

Ordering uses deterministic tuples rather than wall-clock race order:

```text
(earliest_start, priority_class, opportunity_score,
 source_group, watch_id, job_id)
```

### 16.3 Leases and concurrency

- one current lease per logical job;
- bounded lease time;
- append-only renewals;
- deterministic expiry recovery;
- per-domain concurrency ceilings;
- global response-byte and CPU budgets;
- no two workers may finalize the same logical job;
- duplicate terminal records are rejected by identity.

### 16.4 Clock behavior

Wall-clock time determines due intervals and freshness. An injected monotonic
clock measures timeouts. Tests use deterministic clocks. Clock rollback or large
jumps produce explicit schedule diagnostics.

## 17. CLI and Command Surface

Add stable JSON operations:

- `internet-watch` — `register`, `supersede`, `enable`, `disable`, `list`, and
  `get` watches;
- `internet-poll` — `schedule`, `select`, `lease`, and `list` bounded jobs;
- `internet-fetch` — `execute` one explicit policy-bound fetch or `list`
  receipts;
- `internet-source` — `get` a persisted source record or `assess` a snapshot;
- `internet-extract` — `execute` deterministic extraction or `list` receipts;
- `internet-candidate` — `get`, `explain`, or explicitly `migrate` immutable
  legacy candidate lineage;
- `internet-improvement` — `feed`, `reason`, `experiment-qualify`,
  `policy-assess`, `probation-admit`, `probation-select`, `probation-observe`,
  `advance`, or report `status`;
- `internet-promotion-policy` — `register`, `register-default`, `assess`,
  `get`, or `list` exact policy records;
- `internet-probation` — `admit`, `select`, `observe`, or `list` probation
  state;
- `internet-integrity` — `verify` immutable records and projections or
  `rebuild` derived projections.

There is no `approve` operation in the SAA internet promotion lifecycle.

CLI defaults are read-only unless the requested operation writes StateWright's
internal immutable stores. Internet operations cannot request arbitrary command
execution or workspace mutation.

## 18. Persistence Layout

```text
<workspace>/.ourd-agent/egcf/
  objects/sha256/<prefix>/<digest>.json
  artifacts/sha256/<prefix>/<digest>
  events.jsonl
  projection.sqlite3
```

Every internet lifecycle fact is an ordinary typed immutable EGCF record in the
shared content-addressed object store. Snapshot bodies are immutable artifacts,
while fetch receipts, source snapshots, assessments, extractions, fragments,
candidates, qualifications, probation records, promotion or demotion decisions,
and supersedence records are canonical JSON objects. There is no parallel
internet persistence authority.

Projection schema version 3 adds the rebuildable `internet_records` and
`internet_record_fts` tables plus exact type/status, snapshot, and candidate
indexes. Projection rebuild validates row counts, extracted fields, canonical
payload parity, and FTS parity against immutable objects.

Snapshot bodies are immutable binary files addressed by digest. Their metadata
is represented by canonical JSON records; binary bytes are not embedded in JSON.

The first release executes only exact scalar one-input/one-output `IDENTITY` and
`CONST` internal experiment programs. Probationary canonical admission is
further restricted to exact `IDENTITY` candidates. Downloaded source text is
never compiled or executed.

## 19. Security and Abuse Controls

Required adversarial controls:

- SSRF and DNS-rebinding rejection;
- redirect-to-private-network rejection;
- prohibited port and scheme rejection;
- no ambient proxy, credentials, cookies, or local configuration;
- TLS peer and hostname verification;
- header, body, decompression, nesting, and parser limits;
- compression-bomb and archive-bomb rejection;
- malformed Unicode and encoding rejection;
- MIME sniffing cannot override prohibited declared types without an explicit
  policy rule;
- prompt injection retained only as quoted source data;
- HTML, Markdown, JSON, and code parsers run with bounded recursion and memory;
- no downloaded executable permission;
- no shell interpolation;
- no source-controlled command strings;
- no automatic expansion to linked domains;
- domain rate limits and retry ceilings;
- sensitive response headers are redacted before persistence;
- raw snapshot access is explicit and auditable.

## 20. Observability

Expose deterministic metrics and projections for:

- watch count and due jobs;
- fetch success, failure, bytes, latency, redirects, and policy blocks;
- snapshot reuse ratio;
- extraction yield and quarantine reasons;
- duplicate, equivalent, transfer, adaptation, and novelty rates;
- OIEC-SR proposal and parser-failure counts;
- experiment qualification rate;
- probation promotion and demotion rates;
- retrieval precision and failure avoidance;
- source diversity and freshness;
- canonical contradiction, drift, and false-admission rates;
- scheduler backlog and expired leases.

Metrics are derived from immutable records. They cannot mutate policy or promote
knowledge.

## 21. Migration and Compatibility

1. Add `statewright_sources` without changing existing public SAA behavior.
2. Add StateWright-only record schemas under the extension namespace.
3. Bump the EGCF projection version only when new derived tables are introduced.
4. Preserve frozen EGCF v1 resource hashes.
5. Keep existing `brain-feed` and `repository-feed` CLI operations unchanged.
6. Introduce internet operations as additive `statewright.cli.v1` operations.
7. Existing canonical algorithms receive no synthetic internet provenance.
8. Existing promotion assessments remain valid under their original version.
9. Automatic promotion applies only to candidates bound to an
   `AutonomousPromotionPolicy`.
10. SAA promotion no longer consumes or creates human approval records.
11. EGCF execution approvals remain unchanged and do not flow into SAA identity.

## 22. Dependencies

### Required

- libcurl for bounded native HTTP and HTTPS;
- OpenSSL through the existing dependency for hashes and TLS identity reporting;
- SQLite for rebuildable projections;
- GMP/GMPXX for exact SAA arithmetic;
- nlohmann/json for canonical contract handling;
- Threads for scheduler workers and cancellation.

### Dependency gate

Phase I0 freezes exact versions, ABI identities, licenses, build flags, and
package provenance in `third_party/manifest.lock.json`. No ambient unrecorded HTTP
library is permitted.

## 23. Implementation Phases

## Phase I0 — Freeze Requirements and Threat Model

### Work

- inventory current brain feed, SAA, governance, improvement, evidence, and
  canonical-store interfaces;
- freeze exact dependency and platform assumptions;
- define the internet source threat model;
- define record schemas and lifecycle matrix;
- define autonomous policy defaults and prohibited capabilities;
- create accepted and rejected fixture inventories;
- record source and resource manifests.

### Gate I0

- every semantic fact has one named canonical owner;
- no internet path can directly invoke canonical admission or command execution;
- every lifecycle state and transition is enumerated;
- no per-candidate human approval remains in the SAA promotion design;
- the execution-authority separation is explicit.

## Phase I1 — Source Contracts and Native Library

### Work

- add the `statewright_sources` CMake target;
- implement URL, request, response, policy, snapshot, job, and lease records;
- implement canonical serialization and typed identities;
- define `HttpFetchProvider` and deterministic test doubles;
- add dependency lock entries and build/package rules.

### Gate I1

- all records round-trip canonically;
- rejected URL, policy, and limit fixtures fail closed;
- source library has no SAA, reasoning, EGCF, or application dependency;
- package manifests include schemas and policies.

## Phase I2 — Immutable Internet Store and Projection

### Work

- implement immutable filesystem layouts;
- implement snapshot-body content addressing;
- implement EGCF record registration and event chaining;
- add SQLite projection tables and FTS indexes;
- implement projection validation and rebuild;
- implement supersedence and repeated-fetch reuse.

### Gate I2

- tampered records or bodies are rejected;
- repeated identical bytes reuse one snapshot;
- projection deletion and corruption rebuild exactly;
- interrupted writes cannot create valid partial records;
- concurrent duplicate registration is idempotent.

## Phase I3 — Secure HTTP Acquisition

### Work

- implement the libcurl provider;
- implement strict URL and redirect validation;
- implement public-address resolution and pinning;
- disable ambient credentials, cookies, and proxy authentication;
- implement size, decompression, header, redirect, and time limits;
- implement conditional requests;
- retain bounded transport evidence.

### Gate I3

- SSRF, DNS rebinding, redirect escape, prohibited ports, compression bombs,
  timeout, cancellation, malformed headers, TLS failure, and oversize responses
  fail closed;
- no response bytes bypass immutable capture;
- provider cannot write SAA or EGCF canonical records.

## Phase I4 — Persistent Watches and Scheduler

### Work

- implement watch versioning and supersedence;
- implement deterministic due-job creation;
- implement append-only leases, expiry, retry, and recovery;
- implement domain and global budgets;
- implement bounded worker concurrency and cancellation;
- add deterministic clock injection.

### Gate I4

- restart resumes unfinished work without duplicate logical jobs;
- expired workers cannot finalize jobs;
- domain and global budgets are enforced;
- clock anomalies produce explicit diagnostics;
- scheduler ordering is deterministic.

## Phase I5 — Policy Assessment and Extraction

### Work

- implement robots, license, MIME, encoding, and admissibility assessments;
- implement deterministic initial extractors;
- bind every fragment to exact snapshot evidence;
- add correction, version, and retraction extraction;
- implement extraction limits and quarantine reasons.

### Gate I5

- extraction is byte-reproducible from the frozen snapshot;
- unsupported or ambiguous content is quarantined;
- prompt injection cannot invoke tools or alter policy;
- source corrections create new records rather than mutation.

## Phase I6 — Brain Feed and Existing-Knowledge Retrieval

### Work

- implement `InternetFeedCoordinator`;
- translate extracted fragments to existing brain-feed kinds;
- run exact, semantic, mathematical, relation, transfer, adaptation, and failure
  searches;
- persist complete candidate and exclusion explanations;
- stage only complete validation-ready candidates.

### Gate I6

- duplicate and equivalent algorithms do not create new canonical candidates;
- every novelty result contains complete search evidence;
- incomplete semantics or provenance are quarantined;
- internet routing does not bypass `BrainFeedProcessor`.

## Phase I7 — OIEC-SR Advisory Analysis

### Work

- build bounded source-fragment reasoning context;
- generate competing semantic and algorithm hypotheses;
- generate contradictions, counterexamples, and falsifiers;
- request missing evidence and experiments;
- retain provider and parser provenance;
- add deterministic fallback behavior when providers are unavailable.

### Gate I7

- malformed, timed-out, cancelled, or over-budget provider output fails closed;
- provider output cannot set authoritative lifecycle states;
- all claims link to source snapshots and exact model requests;
- deterministic operation continues without provider availability.

## Phase I8 — Experiments and Integrity Qualification

### Work

- translate candidates into supported internal SAA IR;
- create controlled baseline/candidate experiments;
- aggregate repeated independent evidence;
- register benchmark gates, failure observations, integrity snapshots, and
  trajectories;
- integrate improvement scheduling and evolution records.

### Gate I8

- downloaded code is never executed;
- experiments bind identical frozen contexts;
- hard invariants cannot be traded for score;
- known equivalent failures block blind retries;
- benchmark and integrity results persist and rebuild exactly.

## Phase I9 — Autonomous Promotion Policy

### Work

- implement `AutonomousPromotionPolicy` parsing and canonical identity;
- extend pure promotion governance with policy-bound hard predicates;
- implement `AutonomousPromotionController`;
- persist complete autonomous assessments;
- remove SAA promotion dependence on human approval records;
- add policy-resource manifests and CLI inspection.

### Gate I9

- identical inputs and policy produce the identical decision signature;
- any failed hard predicate blocks promotion;
- model confidence and source popularity cannot satisfy a gate;
- policy mismatch or missing policy fails closed;
- no approval object is requested, created, or consumed.

## Phase I10 — Probation and Automatic Demotion

### Work

- implement probationary canonical state;
- implement bounded canary retrieval;
- register probation observations;
- implement automatic full promotion;
- implement automatic demotion and previous-preference restoration;
- register failure knowledge and re-evaluation schedules.

### Gate I10

- probation cannot silently become full canonical preference;
- successful windows promote without human interaction;
- injected regressions demote automatically;
- historical admission and evidence remain immutable;
- retrieval never selects a demoted algorithm as preferred.

## Phase I11 — CLI, Operations, and Release Qualification

### Work

- add stable JSON CLI operations;
- add integrity, replay, rebuild, and scheduler diagnostics;
- package schemas, policies, and fixture metadata;
- run complete developer, sanitizer, release, and package suites;
- generate source-bound release evidence;
- document residual risks and operational limits.

### Gate I11

- a clean install runs the supported internet fixture smoke tests;
- no Python runtime is required;
- no live internet is required for the default test suite;
- complete source, dependency, policy, test, and package hashes are recorded;
- persistent writers are isolated during final evidence generation;
- autonomous promotion and demotion pass end to end without human approval;
- release status does not claim EGCF execution authority expansion.

## 24. First Integrated Delivery Slice

The first useful vertical slice will:

1. start a deterministic local HTTP fixture server;
2. register one immutable internet watch;
3. create and lease one due fetch job;
4. fetch, hash, and persist one exact source snapshot;
5. assess source and transport policy;
6. extract one algorithm description and its benchmark table;
7. create and process one brain-feed batch;
8. prove the candidate is not an exact or equivalent existing algorithm;
9. generate bounded OIEC-SR interpretations and falsifiers;
10. translate the selected proposal into supported SAA IR;
11. execute baseline and candidate experiments on a frozen fixture;
12. pass benchmark, invariant, evidence-independence, and integrity gates;
13. evaluate the packaged autonomous promotion policy;
14. admit the algorithm as `PROBATIONARY_CANONICAL` without approval;
15. exercise bounded canary retrieval windows;
16. automatically promote after successful windows;
17. inject a deterministic regression in a separate run;
18. automatically demote and restore the previous preferred algorithm;
19. rebuild every projection from immutable records; and
20. replay the complete decision chain with identical signatures.

This slice uses no external internet, no downloaded-code execution, no human
approval, and no expanded command authority.

## 25. Validation Strategy

### 25.1 Test layers

1. **Unit:** canonical records, URL parsing, policies, scoring, predicates, and
   state transitions.
2. **Contract:** schemas, typed IDs, canonical JSON, resources, CLI envelopes,
   and dependency identity.
3. **Persistence:** immutable writes, body hashing, event chains, projection
   rebuild, restart, and supersedence.
4. **Network security:** SSRF, DNS rebinding, redirects, TLS, credentials,
   decompression, MIME, timing, and cancellation.
5. **Extraction:** byte binding, parser limits, malformed content, correction,
   and prompt-injection isolation.
6. **Retrieval:** exact, equivalence, relation, transfer, failure, exclusions,
   deterministic ordering, and freshness.
7. **Reasoning:** bounded context, malformed providers, competing hypotheses,
   contradictions, falsifiers, and no-authority properties.
8. **Experiment:** frozen context, exact metrics, invariants, independence,
   repeated aggregation, and known-failure blocking.
9. **Promotion:** policy identity, conjunctive gates, deterministic signatures,
   and absence of approval dependencies.
10. **Probation:** canary bounds, observation windows, promotion, demotion, and
    preference restoration.
11. **Fault:** crash between every durable step, expired leases, partial body
    capture, stale projections, and disk failures.
12. **Property:** idempotency, identity stability, deterministic ordering, and no
    transition laundering.
13. **Performance:** bounded memory, response streaming, scheduler scale,
    projection latency, and first-use admission cost.
14. **Sanitizer:** address, undefined behavior, leak, and thread checks.
15. **Package:** empty-prefix install, resource discovery, policy manifest, and
    local fixture smoke.
16. **Release:** frozen generation, complete requirement matrix, evidence bundle,
    residual risks, and replay.

### 25.2 Mandatory adversarial fixtures

- redirect loop;
- public host redirecting to loopback;
- DNS answer change after validation;
- oversized header and body;
- compression bomb;
- misleading MIME type;
- invalid UTF-8 and deep nesting;
- hidden prompt injection;
- duplicate content at different URLs;
- multiple pages from one source group;
- contradictory independent sources;
- retraction and correction;
- model proposal claiming false qualification;
- mathematically equivalent renamed algorithm;
- known failure recurrence;
- benchmark improvement with invariant regression;
- probationary retrieval regression;
- crash after record write but before projection update;
- stale lease completion attempt;
- policy version mismatch;
- missing demotion path.

## 26. Completion Definition

Implementation is complete only when:

- all phases I0-I11 pass their gates;
- the internet acquisition path is bounded and adversarially tested;
- exact snapshots and all decisions are immutable and replayable;
- existing-knowledge-first retrieval prevents duplicate canonical ownership;
- OIEC-SR remains advisory and source-bound;
- experiments execute only supported internal representations;
- autonomous promotion is deterministic, policy-bound, and approval-free;
- probationary knowledge is bounded and observable;
- regressions automatically demote and restore valid prior preference;
- canonical admission remains owned by `CanonicalAlgorithmStore`;
- SAA status does not grant command execution authority;
- persistent writers are isolated during release evidence generation;
- a clean Linux package passes full fixture-based qualification; and
- residual limitations are explicitly recorded without claiming general web
  crawling, authenticated browsing, arbitrary-code learning, or autonomous EGCF
  mutation authority.
