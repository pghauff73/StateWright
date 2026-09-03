# Residual Risks

This file records known limitations at the current implementation checkpoint.
None is implicitly accepted for production cutover.

| Area | Residual risk | Current control |
| --- | --- | --- |
| Cutover | Python source owners have not been retired and production authority has not moved. | Cutover remains `PENDING_HUMAN_APPROVAL`; no automatic flag is enabled. |
| Command fabric | C0-C2 catalog commands without native domain semantics use a deterministic generic read-only adapter. | The adapter cannot invoke external executors or mutate workspace files and reports explicit limitations. |
| Mutation coverage | The admitted C3 vertical slice supports one transaction per plan; other C3 and all C5 commands fail closed. | Exact approval, plan binding, verification, and rollback are required for `eon.execute@1`. |
| Repository feed | Static source scanning is bounded and intentionally shallow rather than a complete parser for every language. | Candidates are staged or quarantined and never self-admitted as canonical algorithms. |
| Persistence batching | Immutable object files and ledger events are append-only but a process failure can interrupt a multi-record registration prefix. | Startup projection validation/rebuild recovers derived SQLite state; interrupted canonical admission still requires operator review. |
| Schema parity | The frozen EGCF v1 schema uses oracle-default semantics that differ from stricter native validation in a few optional/defaulted fields. | The frozen schema remains byte-identical; StateWright-only records live in a separate additive extension schema. |
| Error taxonomy | Some malformed persisted JSON is reported through a broader filesystem/store error category. | Failures remain closed and retain the original diagnostic text. |
| Platform | Locking, executable discovery, persistence, and release qualification are Linux-only. | macOS and Windows are explicitly outside the first release scope. |
| Unicode | Selected semantic normalization paths use ASCII lowercase rather than full Unicode case folding. | Canonical JSON and UTF-8 validation remain deterministic; non-ASCII semantic equivalence is not claimed. |
| Exact arithmetic serialization | Several GMP-backed JSON projections convert through machine-sized integers. | Algorithms remain exact internally; very large numerator/denominator serialization is not release-qualified. |
| Numerical adapters | MIMO/nonlinear floating adapters require more explicit NaN/overflow policy coverage. | Exact rational paths and bounded domain checks are authoritative where available. |
| Conflict scope | Assurance conflict queries currently operate over the shared invariant/decision store. | Results are advisory evidence and cannot grant approval. |
| Performance | Large first-use catalog admission and full startup integrity validation remain I/O-heavy. | Normal writes update SQLite incrementally; explicit benchmarks must be reviewed before release. |
| Internet candidate scope | First-release internal experiments support exact scalar one-input/one-output `IDENTITY` and `CONST`; probationary canonical admission supports exact `IDENTITY` only. | Unsupported SAA IR and non-identity probation admissions fail closed without executing downloaded code. |
| Internet scheduling | Acquisition scheduling and leases are local to one Linux host rather than distributed across workers. | Immutable generation-bound jobs, exclusive leases, expiry handling, and duplicate-terminal-record rejection provide single-host crash recovery. |
| Internet access | The fetcher does not support authentication, cookies, browser profiles, JavaScript, CAPTCHA, arbitrary crawling, or a general browser engine. | Only explicit unauthenticated HTTP/HTTPS watches under immutable source policies are admitted. |
| Internet fixture portability | The local fixture server and qualified acquisition runtime use Linux/POSIX sockets. | The default suite requires neither Python nor live internet; macOS and Windows remain outside first-release qualification. |
| Internet freshness | Source age is calculated from the exact fetch lease acquisition timestamp, while experiment-only evidence uses its immutable creation timestamp. | Canonical UTC parsing is strict; missing, malformed, future, or stale lineage fails the autonomous policy closed. |
| Internet capture failure | A fetch failure attempts to persist its failure receipt and close the lease; a simultaneous store or disk failure may mask the original transport diagnostic. | The operation still fails closed and immutable replay exposes any durable prefix; compound fault injection remains a qualification extension. |
| Internet CLI type narrowing | `internet-source get` retrieves an exact EGCF record ID and does not independently restrict the result to an internet-source object type. | Typed IDs, exact object signatures, and caller-selected immutable IDs prevent mutation, but stricter command-level narrowing remains desirable. |
| Internet performance coverage | Core bounds, crash recovery, projection rebuild, and adversarial network cases are tested, but scheduler-scale and exhaustive disk-fault matrices are not complete. | Release claims remain limited to the packaged single-host fixture and recorded test matrix. |
