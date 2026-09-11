# Persistent SAA repair v6

## Strategy and implementation

1. Preserve successful action deduplication. Keep policy and semantic failures
   deferred with explicit diagnostics. Permit receipt-linked, distinct attempts
   for filesystem failures and stale preconditions within the action retry
   ceiling, capped at three retries. Reasoning permits two retries. Existing
   fetch-job attempt and resource gates remain unchanged.
2. Preserve full authority and projection validation on each store open and
   explicit integrity verification. Avoid redundant full validation during
   planning within that same locked store. Filter internet records and pending
   recovery records in SQL before materializing JSON payloads.
3. Keep deterministic logical request time for planning, but advance execution
   time with a monotonic clock. The CLI includes store-open time. Inject clocks
   in boundary tests. Check dispatch and lease windows, record execution and
   completion separately, and label late completions without replaying effects.
4. Return fresh no-work plans without persisting domain runs or events. The
   supervisor retains operational poll evidence. Recovery of previously stored
   runs still records a terminal outcome.

## Compatibility and limits

No stored receipts are rewritten. No source, experiment, promotion, or probation
requirement is weakened. Failed recovery returns FAILED or STALE, not successful
RECONCILED. Fresh no-work responses intentionally have empty run and plan IDs;
callers should inspect status before dereferencing these IDs.

Full store-open validation remains proportional to historical store size. This
repair removes redundant work rather than claiming constant-time startup or a
complete incremental-integrity implementation. SQL pending-run selection still
needs scaling measurement. Policy failures are not automatically retried.

## Validation plan

Build the CLI, supervisor, and contract tests. Run reasoning, director,
orchestrator, supervisor, experiment, and store regression coverage plus CLI
orchestration smoke checks. Exercise idle polling, failure classification,
retry exhaustion, worker-filtered recovery, and an injected late execution
clock. Follow with bounded live processing, fresh metrics, and store integrity.
