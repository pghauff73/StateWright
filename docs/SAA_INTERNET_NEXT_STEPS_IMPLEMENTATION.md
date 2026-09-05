# SAA internet improvement: implemented next steps

Implementation checkpoint: 2026-09-05 (Australia/Brisbane). This extends the
existing native pipeline; it does not enable a production watchlist or broaden
EGCF command authority.

## Safety and recovery

- Director fetch selection now uses the shared scheduler's due windows,
  concurrency, byte/CPU reservations, retry ceilings, and active watch lineage.
  Orchestrator and source coordinator revalidate before acquisition. Disabled,
  superseded, future, expired, or already completed jobs cannot fetch.
- Watch enable/disable and scheduling-only successors carry signed registration
  provenance. Changes to the URL, source identity, policy, or transport constraints
  cannot inherit eligibility. Imported preflight reports bind both registry and
  exact policy; their claimed eligibility is recomputed from transport, license,
  and robots evidence. A report hash establishes integrity, not third-party trust.
- Curated registration retains the extraction strategy and publisher independence
  group. Unknown, review-required, and prohibited licenses cannot become eligible
  by importing a report that says otherwise.
- Feed replay reconciles durable batches, fragment dispositions, retrieval receipts,
  and candidates, including interruptions between individual fragment records.
- Promotion, admission, and probation observations check source age at the supplied
  current time. Newer denials override older permission; equally recent denial wins.
  Experiment timestamps cannot keep a source fresh indefinitely.
- Conditional requests reuse receipt-backed ETag/Last-Modified validators for the
  same URL and policy. A validated HTTP 304 reuses immutable snapshot bytes while
  recording fresh acquisition and source-assessment evidence.

## Extraction and discovery

HTML extraction retains paragraph/preformatted context across inline elements,
code line breaks, table cells, source byte spans, and standalone raw MathML.
Script/style/noscript bodies are excluded. This remains a bounded conservative
extractor, not a browser DOM or a general mathematical parser.

Registered `crossref-json` and `europe-pmc-json` strategies extract individual
citations and disabled discovery proposals. DOI/URL duplicates are collapsed;
each proposal retains its snapshot, fragment, and JSON selector. No discovered
link is fetched or enabled automatically.

Inspect the review queue:

```sh
./build/statewright internet-extract '{"workspace":"/path/to/store","action":"proposals","limit":100}'
```

Proposals are not directly registerable watchlist manifests. Assign a reviewed
source group/publisher, review license evidence, create a manifest through
`internet-watchlist create`, preflight it, then explicitly register eligible
entries. The curated discovery services still require license review; extraction
support does not silently relax that gate.

## Exact algorithm scope

Source-to-IR translation requires a complete explicit procedure, for example:

```text
Affine calibration; inputs: x; outputs: y; procedure: return -3/2*x+5/4
Identity; inputs: x; outputs: y; procedure: return the input
```

The supported new family is exact scalar affine arithmetic with nonzero rational
slope, represented by bounded MULTIPLY and ADD nodes. Numeric coefficients have
bounded syntax. Keyword mentions, negated statements, branches, missing ports,
and ambiguous procedures are quarantined. Extracted or downloaded code never runs.

Qualification retranslates the immutable source and checks IR equality. A wrong
expected output is a hard failure, not an averaged-away score. Affine evidence
requires at least two distinct trials in each of at least two independent groups,
with disjoint inputs. Canonical semantic identity includes slope and bias, so
different affine maps cannot collapse under structural normalization. The family
supports probation, successful promotion, regression demotion, and preference
restoration. Internal exact CONST experiment compatibility remains; arbitrary
source constants and general algorithms are not newly supported.

Canonical numeric domain bounds must round-trip exactly through the existing
double-based bound representation. General cryptographic, numerical, and
branching algorithms remain unsupported regardless of source authority.

## Compatibility and operation

- CLI promotion assessment and probation admission now require canonical UTC
  `current_timestamp`. The HOWTO and smoke fixtures supply it explicitly.
- Old signed retrieval and registration records remain readable; optional new
  fields are omitted when absent to preserve their original signatures.
- Regenerate old preflight reports: registry/policy bindings are now required.
  Old promotion assessments without an assessment timestamp cannot newly admit
  a candidate; reassess against current evidence instead.
- The source extension schema and its resource manifest hash are updated. Frozen
  EGCF contracts are unchanged. Historical audit results remain historical.
- No recurring supervisor, timer, public watch, or external service was installed
  in the user's working store by this implementation.

See `SAA_INTERNET_OBSERVABILITY.md` for metrics and history benchmark commands.
The Director now reuses its parsed job-ID index when checking scheduled jobs,
avoiding rehashing all historical jobs once per watch. Source trimming is linear
rather than repeatedly erasing the first character.

## Validation

The final Debug build passed `cmake --build build -j 2` and all 15 checks in
`ctest --test-dir build --output-on-failure -j 2` (117.22 seconds). The checks
include the comprehensive C++ suite, installed-package smoke, executable HOWTO,
watchlist CLI, orchestration fault recovery, supervisor, resource verification,
and the history benchmark smoke. Local loopback access was enabled for fixture
HTTP servers; the suite itself required no public internet.

The C++ suite covers 425 test cases, including strict translation negatives,
affine qualification/promotion/demotion, stale evidence, conditional fetches,
disabled and expired jobs, registration provenance, discovery extraction, metrics,
and mixed-fragment durable-prefix recovery. Shell syntax and `git diff --check`
also passed. Sanitizer and release presets were not rerun for this checkpoint;
the older audit's preset matrix is not reused as evidence for these changes.

Measured 1,000-watch medians were 976.90 ms for pending jobs and 1,434.10 ms
for completed, source-blocked fetches. Full scope and smaller sizes are in
`SAA_INTERNET_OBSERVABILITY.md`. The initial pre-optimization benchmark was stopped
before completing the largest case; no measured speedup ratio is claimed.

### Bounded live acquisition pilot

An isolated temporary store fetched RFC 5869 over verified HTTPS, with robots
permission and the curated IETF license declaration. Four bounded actions
completed: fetch, assess, extract, and feed. The response contained 9,806 compressed
bytes / 32,664 decompressed bytes; the recorded fetch duration was 1,058 ms.
Preflight was a separate request and is not included in store metrics.

The pilot produced 14 fragments and four quarantined candidates, zero accepted
algorithms, and passed immutable-store integrity verification. RFC prose is outside
the exact procedure grammar: this demonstrates safe acquisition and rejection,
not successful autonomous cryptographic learning. Cost per accepted algorithm is
therefore null. Promotion and candidate advancement were disabled.
The pilot watch was disabled after measurement, carrying its registration
provenance to the disabled successor; no background process was started.

The temporary evidence store is `/tmp/statewright-internet-pilot.QtklHg/store`;
its manifest and preflight report are in the parent directory. These files are
local ephemeral evidence, not packaged fixtures or a deployed monitor.
