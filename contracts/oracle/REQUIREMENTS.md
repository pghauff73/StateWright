# StateWright Frozen-Oracle Requirement Matrix

**Oracle commit:** `957111e2d5d11dec719c7f993f51644e701fc256`  
**Selection:** Clean Git commit only; uncommitted OIEC files and generated reports are excluded.  
**Status:** Native implementation checkpoint; full cutover and release approval pending

| Requirement group | Canonical Python/resource owners | C++ target | Required evidence |
| --- | --- | --- | --- |
| Canonical JSON, hashes, IDs, UTF-8 | `ourd/egcf/ids.py`, `ourd/reasoning/models.py`, `ourd/persistence.py` | `statewright_common`, `statewright_contracts` | Python-exported accepted/rejected fixtures; exact C++ output equality |
| Workspace identity and scope | `ourd/workspace.py` | `statewright_core` | Path, traversal, symlink, ignore, file-hash, mode, and snapshot fixtures |
| Authority and policy | `ourd/authority.py`, `ourd/policy.py`, `ourd/models.py` | `statewright_core` | Record, risk, scope-intersection, expiry, and rejection parity |
| Ledger and projections | `ourd/persistence.py`, EGCF stores | `statewright_core`, `statewright_egcf` | Event-chain, redaction, tamper, rebuild, and restart fixtures |
| Transactions and rollback | `ourd/transactions.py` | `statewright_core` | Prepare/apply/verify/finalize/discard/rollback and fault fixtures |
| Collision and bounded control | `ourd/cfel.py`, `ourd/oiec.py`, `ourd/loop_control.py` | `statewright_core` | Attempt identity, progress, convergence, collision, and no-blind-retry parity |
| Operational hypotheses | `ourd/hypotheses.py`, runtime hypothesis records | `statewright_core` | Identity, bounded set, evidence linkage, score, falsification, and projection parity |
| OIEC-SR durable records | `ourd/reasoning/models.py` | `statewright_reasoning` | Constructor defaults, invariants, schema, serialization, signature, and rejected fixtures |
| OIEC-SR deterministic kernels | `ourd/reasoning/hypotheses.py`, `topology.py`, `budget.py`, `scoring.py`, `diversity.py`, `contradictions.py`, deterministic adapters | `statewright_reasoning` | Exact decision, rational, update, topology, ranking, and certificate parity |
| OIEC-SR provider orchestration | remaining `ourd/reasoning/` modules | `statewright_reasoning`, `statewright_providers` | Bounded protocol, malformed output, timeout, cancellation, and model-authority tests |
| OIEC-SR benchmarks | `benchmarks/reasoning/`, benchmark and qualification modules | `statewright_reasoning` | Manifest/checksum, run merge, score, eligibility, qualification, and ablation parity |
| EGCF records and lifecycle | `ourd/egcf/models.py`, `ids.py`, `lifecycle.py`, schemas | `statewright_egcf` | Strict record/schema and lifecycle transition parity |
| EGCF immutable storage | EGCF store and canonical-store modules | `statewright_egcf` | Typed object, tamper, event, projection rebuild, supersedence, and migration parity |
| Capabilities and qualification | capability, registry, qualification, selection modules | `statewright_egcf` | Requirement union, authority intersection, qualification context, exclusion, and tie-break parity |
| Typed command compilation | catalog, context, compiler, handlers, engine | `statewright_egcf` | Catalog/schema, modifier inheritance, immutable DAG, plan hash, and rejection parity |
| Simulation, approval, execution | simulation, approval, assurance, adapters | `statewright_egcf`, `statewright_application` | Same-plan simulation/execution, approval forgery rejection, transaction-only mutation, and rollback evidence |
| Canonical algorithm IR | algebra primitives, models, IR, graph | `statewright_saa` | Parse, canonicalization, graph validation, exact ID, and accepted/rejected parity |
| Normalization and dynamics | normalization, dynamics, MIMO modules | `statewright_saa` | Exact rational, bounds, provenance, round-trip, dimension, and dynamics parity |
| Representative and semantic forms | representative, semantic, units, ontology, alignment modules | `statewright_saa` | Canonical form, provenance, meaning, units, ontology, and revision parity |
| Nonlinear algebra | nonlinear local/global/control/stability/Lie/jet/lift/transform/evidence/remainder modules | `statewright_saa` | Frozen numerical policies, invariants, evidence, and rejected-domain parity |
| Reasoning algebra | reasoning semantics, fit, equivalence, composition, outcome modules | `statewright_saa` | Relation identity, composition, outcome adapter, and non-conflation tests |
| Unified retrieval | retrieval and retrieval-explanation modules | `statewright_saa`, `statewright_egcf` | Complete candidates, exclusions, score components, tie-breaks, freshness, and deterministic ordering |
| Transfer and adaptation | transfer, adaptation, lineage, experiments, aggregation modules | `statewright_saa` | Gap classification, invariant preservation, one-dimension changes, lineage, experiment, and promotion parity |
| Failure and improvement | failure algebra, integrity, benchmark gate, intelligence loop, scheduling | `statewright_saa`, `statewright_egcf` | Failure persistence, retrieval effects, admission, promotion, scheduling, and approval gates |
| CLI and shadow protocol | scoped Python CLIs and JSON projections | `statewright`, `statewright-shadow` | Help, exit code, stdout/stderr, JSONL request/response, and package tests |

The matrix is not closed until every row is expanded into immutable fixture IDs
and mapped to the authoritative frozen test inventory.
