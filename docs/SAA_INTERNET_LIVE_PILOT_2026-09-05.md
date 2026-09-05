# SAA live pilot and persisted-history measurements

Date: 2026-09-05 Australia/Brisbane (2026-09-04 UTC). Decision: **do not increase
polling volume yet**. Acquisition and cache revalidation work, but these sources
produced no candidates compatible with the current exact procedure translator.

## Live scope and results

The isolated pilot used [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869.html),
[RFC 6234](https://www.rfc-editor.org/rfc/rfc6234.html), and
[RFC 7693](https://www.rfc-editor.org/rfc/rfc7693.html). All three passed the existing
TLS, public-address, robots, MIME, size, and curated-license checks. No source
policy was weakened. Two explicit sampling passes ran; production watchlists and
polling intervals were not changed. Only fetch, assessment, extraction, and feed
actions were allowed. No model provider or algorithm promotion was invoked.

| Measurement | Initial pass | Repeat pass | Total/final |
| --- | ---: | ---: | ---: |
| Successful new-body acquisitions (HTTP 200) | 3 | 0 | 3 |
| Successful revalidations (HTTP 304) | 0 | 3 | 3 |
| Failed pipeline fetches | 0 | 0 | 0 |
| Unique snapshots | 3 | 0 new | 3 |
| Extracted fragments | 171 | 0 new | 171 |
| Candidate records | 32 | 0 new | 32 |
| Useful candidates (validation-ready or later) | 0 | 0 | 0 |
| Exact/equivalent canonical retrieval duplicates | 0 / 0 | 0 / 0 | 0 / 0 |
| Quarantined candidates | 32 | 0 new | 32 |
| Experiment qualifications attempted / passed / failed | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| Accepted algorithms | 0 | 0 | 0 |
| Recorded compressed body bytes | 82,740 | 0 | 82,740 |
| Recorded decompressed body bytes | 371,248 | 0 | 371,248 |
| Recorded fetch milliseconds | 1,981 | 2,173 | 4,154 |
| Cost per accepted algorithm | Undefined | Undefined | Undefined |

The three cache hits are duplicate-content revalidations, **not** three duplicate
algorithms. The canonical catalog began empty, so zero canonical duplicate matches
is not a recall/precision measurement of retrieval against a populated catalog.

Each of the 32 candidates had all three blockers:

- `MISSING_SEMANTIC_INPUTS`: 32.
- `MISSING_SEMANTIC_OUTPUTS`: 32.
- `UNSUPPORTED_SOURCE_TO_SAA_IR_TRANSLATION`: 32.

These counts overlap. The source documents describe cryptographic algorithms in
prose/code rather than the supported explicit scalar procedure grammar. They were
quarantined before experiment eligibility; they are not 32 failed qualifications.
No applicable live experiment protocol was invented to force a positive outcome.

Preflight was a separate three-request stage: another 82,740 compressed bytes,
371,248 decompressed bytes, and 2,265 recorded milliseconds. Including preflight,
the recorded totals are 165,480 compressed bytes and 6,419 milliseconds. These are
target-response-body and provider timing measurements, not total wire traffic,
end-to-end runtime, CPU costs, or monetary billing. Headers and robots traffic are
not a separately accounted byte-cost item. Zero accepted algorithms makes every
per-accepted cost denominator zero; metrics correctly return JSON null.

Cleanup disabled all three pilot watches. Final integrity verification passed
with three snapshots and 32 candidates; its event head is
`cf968f8f85f78fa353881f141fcf1e959f33522ca095ed3e539d301d8672006c`.

## Conditional HTTP verification

The coordinator's previous implementation already selects receipt-backed ETag and
Last-Modified values for the same URL, source group, and policy, then sends
`If-None-Match` and `If-Modified-Since`. Unsafe header values are rejected. HTTP 304
requires a matching conditional request and final URL before snapshot reuse.

This pilot verified real ETag revalidation on all three URLs. Each repeat reused
its original snapshot, created fresh fetch/policy-assessment lineage, and downloaded
no target body. Extractions remained at three and candidates at 32. The focused
fixture tests additionally verify both outbound validators and refreshed source
age: two test cases / 14 assertions passed with loopback sockets enabled.

## Growing persisted history

`Tools/benchmark_internet_history.cpp` now has a `--stored-root` mode. It extends
one new schema-validated synthetic store at 10, 100, and 1,000 completed-fetch
checkpoints. These contain 71, 701, and 7,001 internet records, plus artifact
metadata. Each checkpoint has one warmup and three measurements that reopen the
store, validate/load its state, and plan; repeated plans must be identical.

The Debug build used GNU C++ 16.1.1. Observed medians in milliseconds:

| Completed fetches / watches | Internet records | Store open | Validated state read | Plan only | Open + read + plan |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 10 | 71 | 49.50 | 70.35 | 15.75 | 135.41 |
| 100 | 701 | 1,693.07 | 1,013.78 | 140.65 | 2,691.86 |
| 1,000 | 7,001 | 5,184.10 | 6,662.34 | 1,458.70 | 14,364.55 |

Each column is independently summarized; component medians need not sum to the
combined median. At 1,000 fetches, combined measured cycles ranged from 12.244 to
16.291 seconds. Repeated plans were identical at every checkpoint. Store opening
and validated reading, rather than the Director alone, dominated these observations.
This adds measured persisted-state overhead to the earlier in-memory benchmark.

The fixture has blocked sources, not a growing candidate/experiment catalog. Watch
count grows together with fetch count; fixed-watch, long-running polling history is
not a separately measured case. Filesystem caches are
warm, and other pilot work shared this host. Fixture creation and output
serialization are outside the timing. This is not a cold-disk or worst-case claim.

## Decision and next gate

Keep existing polling volume. The immediate limitation is source-to-IR coverage,
not acquisition volume. Before expanding the watchlist or frequency:

1. Choose a small source set that actually expresses supported procedures, or add
   one carefully specified translator/algorithm family with negative fixtures.
2. Supply independently grounded experiment protocols for that family; demonstrate
   qualification, probation, and reproducible accepted yield on real sources.
3. Repeat this pilot against a populated canonical catalog and report duplicate
   handling and a defined cost per accepted algorithm.
4. Include candidate/experiment history and uncached startup in the next scale
   benchmark. Do not extrapolate completed-fetch-only timings to those histories.
   Profile validated-state loading before shortening polling intervals; preserve
   integrity guarantees when considering indexed or cached state reuse.

## Reproduction and evidence

```sh
bash Tools/run_internet_live_pilot.sh ./build/statewright ./build/new-live-pilot
./build/statewright_internet_history_benchmark \
  --stored-root ./build/new-stored-history --resources ./resources 10 100 1000
ctest --test-dir build --output-on-failure -R 'internet_.*history_benchmark'
./build/Tests/statewright_contract_tests '*conditional*'
```

Both output directories must be new. The live runner disables its watches on exit,
retains its immutable evidence store, and never starts a timer or supervisor.
Network access is required for the live runner; the conditional fixture tests
require local loopback access. Benchmark smoke tests are offline and also verify
that existing evidence and decreasing checkpoint sizes are rejected.
Both benchmark smoke checks passed, as did the two conditional HTTP test cases
(14 assertions), shell syntax checks, the benchmark build, and `git diff --check`.
The full C++ suite was not rerun for these tooling/documentation-only additions;
the preceding implementation checkpoint records its separate full-suite results.

This run's evidence is retained under
`build/live-pilot-20260904T215548Z/`: `preflight.json`, `registration.json`,
`baseline-metrics.json`, `pass-1-metrics.json`, `pass-2-metrics.json`,
`final-metrics.json`, `receipts.json`, `snapshots.json`, `assessments.json`,
`extractions.json`, per-action results, `disabled.jsonl`, `integrity.json`, and
`stored-history-benchmark.json`. The live store is `store/`; the synthetic benchmark
store is `benchmark-history/`. Build artifacts are local evidence, not committed
production configuration.
