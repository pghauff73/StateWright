# Bounded, resumable SAA ingestion

## Located slow stage

A timed native replay sampled under GDB stopped in `BrainFeedProcessor::feed` while registering a disposition. The stack reached `EgcfStore::cache_appended_authority`, sorting 20,665 object envelopes. Each individual registration also computes the full authoritative checkpoint before updating the projection. This locates an observed persistence hotspot, not a translation rejection or proof that every source has identical performance. Sample: `build/saa-feed-runtime-profile.log`.

## Repair

- Source fragments are registered as one bounded extraction batch before its receipt. Complete fragment text, identifiers, source bindings and historical records are retained.
- Brain-feed items and dispositions are persisted in batches rather than triggering a full-store checkpoint for each item.
- Native `feed` and `reprocess` default to eight intact source fragments per step. Recurring supervisor feed actions always use eight. The existing external 120-second child limit remains in force.
- A `complete: false` feed result is partial progress, not a completed source feed or accepted function. `processed_fragments` counts this step; `total_fragments` identifies the full extraction size.
- Completion checks combine durable chunk receipts and require a retrieval plus candidate for each algorithm fragment. A source-only disposition cannot complete an algorithm fragment.
- Resume skips finished fragments and reuses a previously persisted covering batch and original dispositions after an interruption. It does not relabel those dispositions as newly discovered duplicates.
- Completed-fragment and batch identities participate in the next directed action, so a completed chunk does not suppress subsequent chunks.
- A repeat after full completion performs zero fragment work. The internal C++ API retains its legacy all-at-once default for compatibility; recurring workers explicitly select the bounded path.

## Resumption

For an already extracted source, repeat the native `internet-improvement` action `feed` with the same admissible `policy_assessment_id`, full `extraction_receipt_id`, workspace and resource root. Set `maximum_fragments_per_step` to 8 and retain the 120-second process timeout. Continue in separate invocations until `result.complete` is true. Do not restart the original non-replay-safe URL feeder. Preserve all returned receipts.

The controlled batch request is stored at `build/saa-feed-100-20260905-EqQArB/supervisor/bounded-feed-request.json`. This request resumes the captured XML canonicalization source; it does not fetch it again or broaden source eligibility.

## Validation

The repaired targets built successfully. Focused tests passed 886 assertions in 88 cases, including store-reopen resumption, no-op replay, interrupted retrieval/candidate recovery, brain-feed behavior and repository feeding. Seven selected CLI/supervisor integration checks passed. Logs: `build/saa-bounded-tests.log` and `build/saa-bounded-cli-tests.log`.

Live timing, supervisor and integrity results are recorded separately under the batch's `supervisor` directory. Passing ingestion does not satisfy the independent experiment, reviewer, qualification or probation requirements for acceptance.

## Live resumption outcome

The first real eight-fragment step finished in 53 seconds. Initial supervisor attempts still timed out during planning. A separate timed `run-once` sample located repeated `BrainFeedBatchReceipt::object_id()` hashing inside fragment-completion checks. The final implementation calculates each batch ID once, references rather than copies stored payloads, and parses only the matching snapshot. Sample: `build/saa-run-once-runtime-profile-outer.log`.

After that correction, two consecutive supervisor feed actions completed in 149 seconds total with zero invocation failures, failed actions or stale actions. The limits remained 120 seconds per child and 300 seconds per wake. Evidence: `build/saa-feed-100-20260905-EqQArB/supervisor/bounded-resume-result-v3.jsonl`. The unsuccessful attempts remain recorded separately; their exit status is not treated as successful resumption.

The captured XML source has 24 of 124 source fragments covered by three durable chunk batches. The remaining 100 are not claimed complete. Native integrity passed afterward with 60 snapshots, 520 candidate records and zero probation observations. Evidence: `supervisor/bounded-resume-integrity.json` and `supervisor/bounded-feed-progress.json` under the same batch directory.

The checkpoint now permits captured-feed-only resumption. The recurring request permits only FEED_EXTRACTION; live fetching, candidate advancement and pending watch initialization remain deferred. The prior full request is preserved at the checkpoint's `repair_v5.full_request_template` path. Completion of this backlog must be reported before broader acquisition is restored. No new acceptance evidence was created.

A completed no-op call may return `brain_feed_batch: null`; use `completion_output_ids` to retrieve the durable prior receipts. The fragment budget bounds work units, not the cost of every possible input. The external timeout remains the hard runtime limit, and interrupted work must resume from durable evidence rather than be declared complete.
