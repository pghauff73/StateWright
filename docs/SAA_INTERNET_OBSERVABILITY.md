# SAA internet pilot measurements

Read measurements from an existing pilot workspace:

```sh
./build/statewright internet-improvement '{"action":"metrics","workspace":"/path/to/pilot-workspace"}'
```

This action validates the stored state and may refresh its rebuildable projection.
It does not create acquisition jobs, call providers, change watches, or promote
algorithms. The response includes the event head and projection digest so a
measurement can be associated with a particular stored history. Capture a
baseline and another measurement after a bounded pilot to compare cumulative
counts and costs. Current-state counts are gauges and can decrease when candidates
advance, merge, or are demoted.

The report distinguishes:

- Fetch bodies acquired, HTTP 304 cache hits, failures, recorded compressed and
  decompressed bytes, and recorded fetch milliseconds.
- Current candidate states, after supersedence, from the number of immutable
  candidate versions in `records_by_type`.
- Retrieval novelty and exact/equivalent matches, source policy blockers,
  extraction rejections, candidate quarantine reasons, and qualification results.
- Distinct algorithms ever admitted to probation (`accepted_ever`) from algorithms
  currently probationary or fully canonical. Repeated admissions of the same
  canonical algorithm count once; historical admissions remain counted after
  demotion.

`cost_per_accepted_algorithm` divides cumulative receipt bytes and fetch time by
distinct algorithms ever admitted. Its fields are `null` before the first
admission. Recorded fetch time is not a financial estimate, CPU measurement, or
end-to-end runtime: it excludes unrecorded failure time and work outside fetching.
Reason counts count each reason once per record and do not imply mutually
exclusive causes. An exact/equivalent-match retrieval can contribute to both
match counters.

# Planning with growing history

```sh
./build/statewright_internet_history_benchmark
./build/statewright_internet_history_benchmark 10 100 1000
```

The benchmark builds normalized synthetic state in memory, then measures the
Director on pending-fetch history and completed-fetch history whose sources are
blocked. It reports watch, job, receipt, record, selected-action, and deferred-action
counts alongside minimum, median, and maximum planning milliseconds. Each case
uses one warmup and three measured runs and checks that the plans remain identical.
Optional sizes must be between 1 and 10,000.

There are no HTTP calls, timers, scheduled runs, or persistent workspace writes.
Fixture construction and disk/projection loading are excluded from the timing.
Compare results from the same build configuration and machine; no machine-specific
timing threshold gates correctness. This benchmark exposes history-related
planning costs but does not replace an end-to-end pilot.

## Persisted history and bounded live pilot

Include persisted object/event history and state-loading overhead:

```sh
./build/statewright_internet_history_benchmark \
  --stored-root ./build/new-history-benchmark \
  --resources ./resources 10 100 1000
```

The output directory must not exist, and counts must increase strictly. This mode
grows one schema-validated, synthetic completed-fetch store, including real artifact
bytes, and checks its integrity. It measures reopening the store, validated state
reading, and planning separately, as well as their combined time. One warmup precedes
three measured repetitions at each checkpoint. OS caches are warm; this is not a
cold-disk benchmark. Fixture creation, store destruction, JSON output serialization,
and HTTP are excluded. Candidate/experiment history is not represented. The store is
retained as local evidence and has no timer, process, or network activity attached.

To repeat the opt-in live pilot:

```sh
bash Tools/run_internet_live_pilot.sh ./build/statewright ./build/new-live-pilot
```

This checks three curated RFC URLs, registers only preflight-eligible entries in a
new isolated store, and performs two explicit sampling passes (at most six pipeline
fetches plus preflight and robots requests). Each pass has a 20-action ceiling and
zero retries. It never changes production polling intervals. Only acquisition,
assessment, extraction, and feeding actions are enabled; no model provider or
promotion is invoked. Watches are disabled on normal exit or a handled failure,
and final metrics plus integrity results are saved. Forced process termination or
machine failure can bypass shell cleanup; these isolated watches still have no
background scheduler attached.

The evidence directory retains preflight, per-action results, baseline/pass/final
metrics, receipts, snapshots, and disable receipts. Useful candidates should be
reported as those reaching `VALIDATION_READY` or a later qualified/canonical stage,
not raw extracted passages. Quarantined candidates are not failed experiment runs.
Report content revalidations separately from algorithm retrieval duplicates, and
report zero-admission cost as undefined, never as zero.

See `SAA_INTERNET_LIVE_PILOT_2026-09-05.md` for the latest measured results.
Its persisted-history medians for open + validated read + plan were 135.41 ms,
2,691.86 ms, and 14,364.55 ms at 10, 100, and 1,000 completed fetches respectively.
At 1,000 fetches, planning alone was 1,458.70 ms. Persisted-state overhead must
therefore be included when setting a polling budget; the table below measures
only in-memory planning.

## Measured checkpoint

2026-09-05 local Debug build, GNU C++ 16.1.1, one warmup and three repetitions.
Other validation work ran on the same host; these are observations, not isolated
hardware benchmarks. Median planning time after job-ID index reuse:

| Watches/jobs | Pending fetches | Completed, source-blocked fetches |
| --- | ---: | ---: |
| 10 | 9.61 ms | 13.00 ms |
| 100 | 95.10 ms | 132.49 ms |
| 1,000 | 976.90 ms | 1,434.10 ms |

Pending fixtures contain `2N+1` records; completed fixtures contain `7N+1`.
Plans were byte-identical across repetitions. These cases do not cover growing
candidate/experiment histories or disk loading, and do not establish the worst-case
complexity of every Director join.
