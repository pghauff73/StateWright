# SAA Persistent Internet Improvement HOWTO Writing Plan

**Plan date:** 2026-09-03

**Implementation date:** 2026-09-03

**Target document:** `docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md`

**Status:** Implemented and fixture-scope documentation-qualified

## 1. Objective

Write a source-grounded operational HOWTO that enables a StateWright user to
exercise the qualified SAA persistent internet improvement lifecycle from a
clean build through immutable acquisition, extraction, candidate reasoning,
exact experiment qualification, autonomous probation, promotion, regression
demotion, integrity verification, and diagnosis.

The HOWTO will explain how to operate the implemented subsystem. It will not
restate the architecture plan, claim broader web-agent capability, approve
production cutover, or imply that SAA knowledge promotion grants EGCF command
execution authority.

## 2. Intended Readers

The document will support three progressive reading paths:

1. **First run:** build StateWright and run the packaged loopback fixture from
   start to finish without Python or live internet.
2. **Operator:** create a bounded source policy and watch, execute individual
   lifecycle commands, inspect immutable records, and recover projections.
3. **Maintainer:** trace record identities, verify policy and evidence lineage,
   diagnose failed gates, migrate legacy candidate lineage, and reproduce
   qualification evidence.

Each advanced section must build on the first-run path instead of introducing a
second workflow or competing terminology.

## 3. Canonical Documentation Sources

Every statement and example in the HOWTO must be derived from one of these
authoritative sources:

| Subject | Canonical source |
| --- | --- |
| Scope, invariants, lifecycle, CLI contract, and qualified limits | `docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_PLAN.md` |
| Implemented phase and authority evidence | `docs/SAA_INTERNET_IMPLEMENTATION_AUDIT.md` |
| Exact supported CLI actions and request fields | `Apps/statewright/cli.cpp` |
| Reproducible end-to-end command sequence and assertions | `Tests/internet_cli_smoke.sh` |
| Native loopback fixture behavior | `Tests/sources/internet_fixture_server.cpp` and `resources/fixtures/internet/identity-v1.json` |
| Default source and autonomous promotion rules | `resources/policies/internet/default-source-policy-v1.json` and `resources/policies/internet/default-promotion-policy-v1.json` |
| Persistence and canonical ownership | `Core/src/egcf/store.cpp`, `Core/src/egcf/internet_improvement_store.cpp`, and `Core/src/egcf/internet_probation.cpp` |
| Security and fetch behavior | `Core/src/sources/curl_http_provider.cpp`, `Core/src/sources/fetch.cpp`, and `Core/src/sources/policy.cpp` |
| Release evidence procedure | `Tools/generate_internet_release_evidence.sh` and `docs/RELEASE_QUALIFICATION.md` |
| Known limitations and unresolved work | `docs/RESIDUAL_RISKS.md` |

The HOWTO must link to these owners rather than copying large policy or schema
payloads that can drift. Short excerpts are permitted only when required for a
working command and must name the source file.

## 4. Required HOWTO Structure

### 4.1 Orientation

- define the SAA persistent internet improvement lifecycle in one diagram or
  ordered flow;
- distinguish immutable authoritative records from rebuildable SQLite
  projections;
- state that internet content is hostile data and downloaded code never runs;
- state that autonomous SAA promotion requires no human approval;
- state separately that general EGCF execution and production cutover authority
  remain unchanged.

### 4.2 Prerequisites and Build

- list the qualified Linux/POSIX environment and required build tools;
- configure and build the `developer` or `release` preset;
- locate the `statewright` and native fixture-server binaries;
- verify resource discovery with `statewright version --json` and
  `Tools/verify_release_inputs.sh`;
- recommend a disposable workspace for tutorials so immutable tutorial records
  do not mix with operational state.

### 4.3 Five-Minute Qualified Fixture Run

Provide the shortest supported success path using:

```bash
Tests/internet_cli_smoke.sh \
  build/release/statewright \
  build/release/Tests/statewright_internet_fixture_server \
  resources/fixtures/internet/identity-v1.json
```

Explain that this proves the complete fixture lifecycle, including automatic
promotion, injected regression demotion, preference restoration, projection
rebuild, zero approval records, and rejection of an `approve` action. It proves
only the qualified fixture scope.

### 4.4 Manual Lifecycle Walkthrough

Expand the smoke flow into independently runnable sections:

1. create an isolated workspace and start the native loopback fixture server;
2. register an immutable source policy and watch with `internet-watch`;
3. create and lease a deterministic job with `internet-poll`;
4. capture the exact response with `internet-fetch`;
5. assess source admissibility with `internet-source`;
6. extract snapshot-bound fragments with `internet-extract`;
7. feed fragments through `BrainFeedProcessor` with
   `internet-improvement feed`;
8. inspect and explain the candidate with `internet-candidate`;
9. run deterministic advisory OIEC-SR reasoning with
   `internet-improvement reason`;
10. execute exact internal SAA IR experiments with
    `internet-improvement experiment-qualify`;
11. register or inspect the packaged promotion policy and run
    `internet-improvement policy-assess`;
12. admit the candidate to probation without approval;
13. select canary uses and register the required observations across two
    windows;
14. show automatic promotion to `CANONICAL`;
15. inject the documented regression observation and show automatic `DEMOTED`
    state plus previous-preference restoration;
16. rebuild and verify projections with `internet-integrity`.

Each step must show the input JSON, identify the output fields needed by the next
step, and use shell variables rather than fabricated record IDs.

### 4.5 Inspection and Explanation Recipes

Add task-oriented recipes for:

- listing active watches, jobs, snapshots, candidates, policies, and probation
  observations;
- retrieving one exact immutable record by ID;
- explaining candidate retrieval evidence;
- inspecting candidate status and the next deterministic `advance` action;
- checking source freshness and failed promotion predicates;
- confirming that a demoted algorithm is not preferred;
- rebuilding `internet_records` and `internet_record_fts` from immutable
  objects;
- migrating a legacy candidate by creating an immutable successor rather than
  rewriting the legacy record.

### 4.6 Bounded Real-Internet Operation

Document real HTTP/HTTPS operation only after the fixture walkthrough. Require:

- an explicit immutable source policy and exact URL;
- unauthenticated HTTP/HTTPS only;
- public-address, port, redirect, TLS, MIME, size, decompression, and timeout
  controls;
- a disposable first-run workspace and conservative polling interval;
- inspection of fetch receipts, snapshot hashes, policy assessment, license and
  robots evidence, and source freshness before candidate use;
- no cookies, browser profile, ambient credentials, proxy authentication,
  JavaScript, CAPTCHA, unbounded crawl, or downloaded-code execution.

Do not include a public URL as a promised stable fixture. Any illustrative URL
must be marked as replaceable and must not be required by the default procedure.

### 4.7 Troubleshooting

Organize diagnostics by lifecycle boundary rather than generic error text:

- resource-manifest or schema mismatch;
- source-policy rejection, prohibited address, redirect, port, or MIME;
- expired lease or stale schedule generation;
- missing or tampered snapshot body;
- quarantined or empty extraction;
- duplicate/equivalent existing algorithm exclusion;
- unavailable or malformed reasoning provider with deterministic fallback;
- unsupported SAA IR, invariant failure, benchmark failure, or integrity
  failure;
- stale source evidence or promotion-policy mismatch;
- incomplete probation windows or automatic regression demotion;
- stale/corrupt projection requiring `internet-integrity rebuild`;
- legacy candidate requiring `internet-candidate migrate`.

For each case, give the safe inspection command, expected failure boundary, and
recovery action. Never recommend deleting immutable objects, bypassing policy,
editing signatures, or inventing approval records.

### 4.8 Persistence, Backup, and Cleanup

- document `.ourd-agent/egcf/objects`, `artifacts`, `events.jsonl`, and
  `projection.sqlite3`;
- identify objects, artifacts, and ledger events as authoritative;
- identify SQLite projection tables as derived and rebuildable;
- recommend copying the entire workspace state for backup or forensic replay;
- use disposable tutorial workspaces for cleanup rather than deleting selected
  immutable records;
- warn that an incorrect `workspace` field can create `.ourd-agent` state in an
  unintended directory.

### 4.9 Qualification and Evidence

- show the developer, sanitizer, and release CTest commands;
- show the non-overwriting internet evidence command with a date-specific output
  directory;
- explain `QUALIFIED_FIXTURE_SCOPE`, manifest verification, migration hashes,
  package hashes, and authority fields;
- distinguish subsystem qualification from production release approval and
  source-owner cutover.

### 4.10 Limits and Next Steps

End with the exact first-release limits:

- Linux/POSIX and local single-host scheduling;
- unauthenticated HTTP/HTTPS without a browser engine;
- exact scalar one-input/one-output `IDENTITY` and `CONST` experiments;
- exact `IDENTITY` probationary canonical admission;
- no arbitrary crawling, JavaScript, CAPTCHA, model training, downloaded-code
  execution, or automatic EGCF C3/C5 authority.

Link to `docs/RESIDUAL_RISKS.md` rather than presenting these limits as defects
the operator should bypass.

## 5. Example-Writing Rules

1. Derive the canonical command sequence from `Tests/internet_cli_smoke.sh`.
2. Use `jq -cn` or checked JSON files so quoting is reproducible.
3. Use only request fields accepted by `Apps/statewright/cli.cpp`.
4. Capture returned IDs into variables; never paste invented hashes or IDs.
5. Show representative output shapes, not unstable full payload dumps.
6. Label every write operation and the immutable object it creates.
7. Use a temporary workspace in all default examples.
8. Keep provider-backed reasoning optional and show deterministic fallback.
9. Never present model output, popularity, or confidence as promotion evidence.
10. Never add an approval step to the SAA internet lifecycle.
11. Never imply that canonical SAA status permits command or process execution.
12. Mark fixture-only, platform-specific, experimental, and unresolved behavior
    explicitly.

## 6. Traceability Matrix

Before drafting prose, create a working coverage matrix with one row for every
HOWTO step and these columns:

- operator objective;
- CLI operation and action;
- required inputs;
- produced record type and ID;
- canonical implementation owner;
- next-step dependency;
- fixture-smoke line or test evidence;
- authority class and side effects;
- qualified limitation;
- validation command.

The completed HOWTO must cover every stable internet CLI operation at least once
or explicitly classify it as an inspection variant of a covered operation.
Overlapping explanations must link to one canonical section instead of creating
multiple descriptions of the same lifecycle fact.

## 7. Writing Phases

### Phase H0 — Freeze Source and Coverage

#### Work

- verify the resource manifest and current qualification evidence;
- freeze the exact CLI action inventory;
- build the traceability matrix;
- inventory every request field used by the fixture smoke;
- classify implementation facts, operational advice, and residual limits.

#### Gate H0

- every planned claim has a canonical source;
- every stable internet CLI action has a documentation owner;
- no proposed or unsupported capability is presented as implemented.

### Phase H1 — Draft the Fixture Journey

#### Work

- write prerequisites, build, disposable-workspace setup, and five-minute smoke;
- expand the fixture into the manual lifecycle walkthrough;
- capture IDs and output fields deterministically;
- explain promotion, demotion, restoration, and approval rejection.

#### Gate H1

- a clean reader can reproduce the fixture without live internet or Python;
- no command contains a fabricated identifier;
- the walkthrough terminates in verified projection integrity.

### Phase H2 — Add Operator Recipes

#### Work

- add inspection, status, `advance`, migration, rebuild, and diagnosis recipes;
- document persistence ownership and safe disposable cleanup;
- add bounded real-internet policy guidance after the fixture path.

#### Gate H2

- every recipe names its mutation or read-only behavior;
- no recovery step edits or deletes immutable history;
- real-internet guidance remains opt-in and policy-bound.

### Phase H3 — Add Safety and Authority Explanations

#### Work

- document hostile-input handling and prohibited capabilities;
- explain advisory OIEC-SR reasoning and deterministic authoritative gates;
- distinguish approval-free SAA promotion from general EGCF execution approval;
- document first-release limits and residual risks.

#### Gate H3

- no model, source, or CLI result is described as self-authorizing;
- no downloaded content can be interpreted as executable instruction;
- the authority boundary matches release qualification evidence.

### Phase H4 — Validate Every Snippet

#### Work

- extract executable shell blocks into a temporary validation script;
- run them against an isolated workspace and native fixture server;
- assert snapshot hash, candidate primitive, promotion, demotion, restoration,
  zero approval records, and integrity verification;
- validate JSON examples with `jq`;
- check referenced files and headings exist.

#### Gate H4

- every default executable snippet passes from a clean build or is explicitly
  marked illustrative;
- the validation run creates no state outside its temporary workspace;
- no live internet or Python runtime is required.

### Phase H5 — Review for Drift and Usability

#### Work

- compare the draft action inventory with `Apps/statewright/cli.cpp`;
- compare the walkthrough with `Tests/internet_cli_smoke.sh`;
- compare security and limits with policies and `docs/RESIDUAL_RISKS.md`;
- run a novice pass for missing prerequisites and an expert pass for provenance,
  replay, and authority accuracy;
- remove duplicated architecture prose that belongs in the implementation plan.

#### Gate H5

- all commands, action names, fields, states, and limits match current source;
- the HOWTO is usable without reading implementation code first;
- advanced details remain traceable to canonical owners.

### Phase H6 — Integrate and Qualify Documentation

#### Work

- add the HOWTO to the README documentation index;
- add the snippet-validation command to the HOWTO maintenance section;
- include the HOWTO and validation artifact in the next source-bound evidence
  bundle;
- record any unresolved documentation limitation rather than weakening a gate.

#### Gate H6

- repository navigation exposes the HOWTO;
- source and package manifests include the final document where applicable;
- the evidence bundle proves the documented default path;
- documentation does not claim production cutover or expanded command authority.

## 8. Planned Deliverables

1. `docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md` — the complete operator
   guide.
2. `Tests/saa_internet_howto_smoke.sh` — extracted, executable default snippets
   or a thin wrapper around the canonical fixture flow.
3. README documentation-index entry.
4. Updated source-bound internet release evidence containing the HOWTO and its
   validation log.

The validation script must reuse canonical fixture behavior rather than create a
second independent lifecycle implementation.

## 9. Completion Definition

The HOWTO is complete only when:

- a new user can run the native fixture lifecycle from build to integrity
  verification using only the document;
- every command and JSON field is accepted by the current CLI;
- every record transition is explained with its immutable evidence dependency;
- autonomous promotion and demotion are shown without human approval;
- general EGCF execution approval remains explicitly separate;
- no downloaded content, model response, or source popularity is described as
  executable or authoritative;
- persistence, replay, projection rebuild, migration, and safe cleanup are
  operationally documented;
- all default snippets pass in an isolated Linux/POSIX workspace without Python
  or live internet;
- first-release limits and residual risks are visible; and
- the final HOWTO is included in a fresh, non-overwritten, source-bound evidence
  bundle.
