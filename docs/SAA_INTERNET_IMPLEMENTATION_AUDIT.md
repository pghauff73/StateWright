# SAA Internet Implementation Audit

**Audit date:** 2026-09-04

**Scope:** `docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_PLAN.md`, phases I0-I11,
and
`docs/SAA_PERSISTENT_INTERNET_DIRECTOR_ORCHESTRATOR_IMPLEMENTATION_PLAN.md`,
phases D0-D10, and `docs/SAA_INTERNET_SUPERVISOR_MODE.md`

**Result:** PASS for the documented Linux/POSIX fixture scope

This audit binds completion to the implemented source, immutable resource
manifest, ten additive migration receipts, the developer/sanitizer/release
preset matrix, clean-package smoke tests, and the non-overwriting internet
release-evidence generator. It does not approve production cutover or expand
EGCF execution authority.

## Phase Matrix

| Phase | Result | Implementation and gate evidence |
| --- | --- | --- |
| I0 — Requirements and threat model | PASS | The implementation plan names one canonical owner per fact, treats internet content as hostile data, prohibits downloaded-code execution, removes per-candidate SAA approval, and separates SAA knowledge admission from EGCF command authority. Packaged source and promotion policies are manifest-bound. |
| I1 — Source contracts and native library | PASS | `statewright_sources` owns canonical URL, policy, watch, job, lease, receipt, snapshot, extraction, and scheduler types without depending on SAA, reasoning, EGCF, or applications. CMake packages schemas, policies, migrations, and fixture metadata. |
| I2 — Immutable store and projection | PASS | Internet facts use the shared EGCF object/artifact store and event chain. Projection schema version 3 adds rebuildable `internet_records` and `internet_record_fts`; tests cover tamper rejection, snapshot reuse, idempotency, stale projection recovery, and exact rebuild parity. |
| I3 — Secure acquisition | PASS | The native libcurl provider enforces explicit HTTP/HTTPS policy, public-address validation, redirect and port controls, TLS verification, disabled ambient credentials/cookies/proxy authentication, bounded headers/bodies/decompression/time, and per-origin robots evaluation with hashed receipt evidence before immutable capture. Adversarial network fixtures fail closed. |
| I4 — Watches and scheduler | PASS | Immutable watch generations, stable jittered polling windows, deterministic job deduplication, leases, expiry, retry, budgets, concurrency limits, cancellation, and injected clocks are implemented and covered by restart, fairness, ordering, stale-worker, and budget tests. |
| I5 — Policy and extraction | PASS | Exact source-policy assessments and deterministic HTML/Markdown/JSON/text extraction bind every fragment to one snapshot. Robots-backed fetches automatically register assessment input when `UNKNOWN` licenses are policy-permitted; known-license policies remain evidence-bound and fail closed. Unsupported, ambiguous, oversized, malformed, or injected content is quarantined; corrections create new immutable lineage. |
| I6 — Brain feed and retrieval | PASS | `InternetFeedCoordinator` routes fragments through `BrainFeedProcessor`, runs existing-knowledge-first search, persists retrieval receipts and exclusions, blocks exact/equivalent duplicates, and stages only complete validation-ready candidates. |
| I7 — OIEC-SR advisory analysis | PASS | `InternetReasoningCoordinator` persists source-bound advisory hypotheses, contradictions, falsifiers, provider envelopes, parser provenance, and deterministic fallback results for validation-ready and quarantined candidates. Output signatures bind material results, while provider output cannot set authoritative lifecycle state. |
| I8 — Experiments and integrity | PASS | Exact scalar `IDENTITY` and `CONST` candidates run only as internal SAA IR against frozen contexts. Benchmark shape and integrity are prevalidated before durable qualification; hard invariants, known failures, evidence independence, and exact rational aggregation fail closed. Downloaded code is never executed. |
| I9 — Autonomous promotion | PASS | Packaged versioned policies evaluate conjunctive deterministic predicates, including exact source age derived from immutable lineage. Missing, stale, malformed, mismatched, popularity-only, confidence-only, or failed evidence blocks promotion. The SAA path creates and consumes no approval record. |
| I10 — Probation and demotion | PASS | Exact `IDENTITY` candidates enter `PROBATIONARY_CANONICAL`, use bounded deterministic canary selection and observation windows, promote automatically after success, demote on injected regression, restore the previous preference, and remain historically immutable. Demoted algorithms are excluded from preferred retrieval. |
| I11 — CLI, operations, and release | PASS | Stable JSON operations cover watches, polling, fetch, source assessment, extraction, candidates, reasoning, experiments, policy, probation, migration, integrity, and deterministic advancement. Clean installation smoke passes without Python or live internet. Developer, sanitizer, release, package, resource, migration, replay, rebuild, promotion, and demotion gates pass. `Tools/generate_internet_release_evidence.sh` records source, dependency, resource, policy, migration, test, and package hashes without claiming command-authority expansion. |

## Director and Orchestrator Matrix

| Phase | Result | Implementation and gate evidence |
| --- | --- | --- |
| D0 — Freeze and inventory | PASS | The implementation preserves the original dirty README and documentation additions, remains based on `857639cf80c7cb12fbcbefd751328df2cb887a03`, and revalidated the live branch, remotes, object types, migrations, CLI, coordinators, projection, and store-lock behavior. |
| D1 — Records and schemas | PASS | Closed directed actions, plans, runs, run events, action leases, action receipts, experiment protocols, source-assessment inputs, probation-observation inputs, and candidate reasoning lineage are canonical, signed, schema validated, and covered by migrations `0008` through `0010`. |
| D2 — Persistence | PASS | `InternetImprovementStore` remains the single internet lifecycle persistence surface and now owns orchestration records, active protocol/candidate lookup, latest action leases, and unique terminal receipts without adding a parallel database. |
| D3 — Pure Director | PASS | `InternetImprovementDirector` reads normalized immutable state, reuses the source and SAA schedulers, produces deterministic bounded plans, exposes blocked work, requires protocols and observation inputs, and performs no writes or provider calls. |
| D4 — Source coordinator | PASS | Fetch, failure capture, lease closure, robots-backed automatic assessment-input registration, source assessment, and extraction assembly live in `InternetSourceCoordinator`; known-license policies retain the manual provenance-bound path. |
| D5 — One-action Orchestrator | PASS | `run_once` and `resume` register the plan and run, acquire a bounded action lease, revalidate preconditions, execute one closed typed action, persist one terminal receipt, close the lease, and record terminal run events. No generic command executor exists. |
| D6 — Experiment and observation inputs | PASS | Qualification consumes active immutable experiment protocols; source assessment and probation observations require registered provenance-bound inputs instead of Director-invented evidence. |
| D7 — Restart and fault behavior | PASS | Existing terminal receipts reconcile without re-execution, expired action leases form immutable predecessor chains, stale preconditions produce `STALE` receipts, and provider failures produce durable failed action and fetch receipts. |
| D8 — CLI compatibility | PASS | Manual actions remain available. `advance` is a candidate-scoped `run-once`, while `plan`, `run-once`, `resume`, `run-status`, and `explain-action` expose the core orchestration service. `approve` remains unsupported. |
| D9 — Adversarial and integrity gates | PASS | Focused tests cover deterministic ordering, disabled/deferred actions, genesis state, source assembly, durable no-work and scheduled runs, approval rejection, and provider-failure receipts. Existing hostile-source, projection, promotion, and demotion tests remain green. |
| D10 — Documentation and release | PASS | README, HOWTO, implementation plan, goal prompt, command resources, residual risks, release qualification, package smoke, and release-evidence generation describe and verify the bounded single-process system without claiming a multi-process daemon or exactly-once HTTP. |
| D11 — Bounded supervisor | PASS | The installed `statewright-internet-supervisor` executable queries worker-scoped nonterminal status, defers live leases, resumes expired runs, invokes one closed action at a time, enforces cycle, wall-clock, child-output, request-size, timeout, and retry bounds, terminates child process groups on timeout or signal, emits redacted canonical JSONL, and opens a deterministic circuit without adding approval or command authority. |

## Qualification Results

- Contract suite inventory: 398 test cases; the complete suite passed in every
  preset.
- Developer preset: 13 of 13 CTest tests passed.
- Sanitizer preset: 44 of 44 CTest tests passed. Thirty-two deterministic Catch2
  shards retain ASan and UBSan coverage while independent boundary tests run in
  parallel; two consecutive full runs completed in 42.218 and 46.220 seconds.
- Release preset: 13 of 13 CTest tests passed.
- HOWTO and orchestration extension: the current matrix includes
  `statewright_saa_internet_howto_smoke`,
  `statewright_internet_orchestrator_cli_smoke`, and
  `statewright_internet_orchestrator_fault_smoke`, and
  `statewright_internet_supervisor_smoke`; all developer and release entries,
  plus all 44 sanitizer entries, passed on 2026-09-04.
- Sanitizer runtime gate: `Tools/run_sanitizer_qualification.sh` enforces a
  strict result below 60 seconds and is captured by internet release evidence.
- Installed package smoke: passed and found the supervisor executable, both
  internet policies, the extension schema, migration `0005`, and the packaged
  fixture metadata.
- Direct release CLI smoke: autonomous probation, promotion, regression
  demotion, preference restoration, projection rebuild, and zero approval
  records passed without Python or live internet.
- Every Bash block in the operational HOWTO passes `bash -n`; the HOWTO wrapper
  verifies the stable action inventory, canonical references, qualified limits,
  and the complete runtime fixture.
- Explicit `internet-improvement` action `approve`: rejected as unsupported.
- Resource manifest: 42 of 42 files verified.
- Additive migration receipts: `0001` through `0010` parse as valid JSON.

## Authority Audit

- `CanonicalAlgorithmStore` remains the sole canonical algorithm owner.
- The internet lifecycle has no approval operation or approval dependency.
- The Director and Orchestrator cannot grant command authority and never call
  the general EGCF approval path.
- General EGCF exact-plan C3 approval remains unchanged and separate.
- No internet path grants C3/C5 command authority or executes workspace changes.
- OIEC-SR and provider models remain advisory proposal sources only.
- Downloaded text, source, binaries, and embedded instructions never execute.
- Production cutover and release approval remain outside this subsystem result.

## Qualified Limits

- Linux/POSIX, local single-host scheduling, and unauthenticated HTTP/HTTPS only.
- Exact scalar one-input/one-output `IDENTITY` and `CONST` experiments only.
- Exact `IDENTITY` probationary canonical admission only.
- No cookies, browser profiles, JavaScript, CAPTCHA, arbitrary crawling, or
  general search-engine indexing.
- The default qualification suite uses a native loopback fixture server, not the
  public internet or a Python runtime.
- Scheduler-scale performance and exhaustive compound disk-fault matrices remain
  residual work documented in `docs/RESIDUAL_RISKS.md`.
- Orchestration is qualified as sequential bounded child processes under the
  existing workspace lock. Supervisor mode is a timer-invoked bounded oneshot,
  not a continuously running multi-process daemon or exactly-once external
  transport.
