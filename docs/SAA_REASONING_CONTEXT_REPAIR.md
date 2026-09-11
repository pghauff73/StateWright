# Complete-context reasoning budgets

The reasoning coordinator v2 removes the obsolete 1024-byte per-fragment
restriction without increasing the existing 16384-byte total context budget
or the sixteen-fragment limit. Complete sections share the total budget; kind
labels and separators count toward it. No source text is truncated or split.

Dispatch and execution use the same capacity check. Oversized sections are
deferred as REASONING_CONTEXT_CAPACITY_UNSUPPORTED, not retried as executable
actions or characterized as failed algorithms. Supporting sections exceeding
the total budget requires a separately qualified complete-context adapter.

Execution preflight also requires the candidate's own source fragment and
matching snapshot lineage, before any records or provider calls are produced.
Requests record actual context bytes, the budget, and source_truncated=false.
Provider output remains advisory. No experiment, review, source-policy or
operational-probation gate changes.
