# OIEC-SR Reasoning Benchmarks

This directory contains the SR-0 benchmark harness inputs and frozen outputs.

`baseline-v1.json` is a **deterministic development-fixture baseline**. It
validates schema strictness, task ordering, scoring, evidence coverage,
counterexample metrics, provenance, reproducibility, and the A/B/C reporting
shape. It is not a model-backed performance result and cannot support a claim
that OIEC-SR outperforms another system.

The three system identifiers are:

```text
base     provider answer without governed OIEC reasoning
oiec     governed single-path OIEC answer
oiec_sr  bounded multi-path OIEC-SR answer
```

The fixture observations are deliberately recorded data. The provider-bound
runner preserves the same `BenchmarkExecutor` contract and binds the exact
model digest, provider endpoint, context and output limits, observed decoding
fields, source snapshot, hardware inventory, Ollama allocation, token usage,
response hashes, and zero-retry setting.

Generate a new deterministic fixture artifact from the current source only when
creating a new baseline version. `baseline-v1.json` is immutable historical
evidence and must not be overwritten after source drift:

```bash
python3 tools/run_reasoning_benchmark.py \
  --date 2026-08-28 \
  --output benchmarks/reasoning/baseline-v1.json
```

Verify the recorded baseline's internal signature and pinned SHA-256 checksum:

```bash
python3 tools/run_reasoning_benchmark.py \
  --date 2026-08-28 \
  --output benchmarks/reasoning/baseline-v1.json \
  --check
```

To test whether that historical artifact can still be regenerated from the
current checkout, add `--check-current-source`. A failure after source changes
is expected evidence of drift, not permission to rewrite the old baseline.

Run the source-, provider-, model-, and hardware-bound development benchmark:

```bash
python3 tools/run_reasoning_model_benchmark.py \
  --date 2026-08-28 \
  --model qwen2.5:14b \
  --base-url http://127.0.0.1:11434/v1 \
  --context-budget-tokens 2000 \
  --max-output-tokens 2048 \
  --output benchmarks/reasoning/runs/model-bound-2026-08-28-qwen2.5-14b-provider-default.json
```

For the SR-1A qualification run, use a new append-only artifact name:

```bash
python3 tools/run_reasoning_model_benchmark.py \
  --date 2026-08-28 \
  --model qwen2.5:14b \
  --base-url http://127.0.0.1:11434/v1 \
  --context-budget-tokens 2000 \
  --max-output-tokens 2048 \
  --output benchmarks/reasoning/runs/model-bound-2026-08-28-qwen2.5-14b-sr1a-current-source.json
```

The OIEC-SR arm now constructs a signed immutable `HypothesisSet` before path
generation. Fixed-point evidence and CFEL changes produce content-addressed
`HypothesisUpdateRecord` entries, while the legacy dictionary remains a
validated derived projection. RuntimeState schema v4 migration and update
replay are part of the source-bound qualification surface.

For the SR-2A grounded-topology qualification run, use another append-only
artifact name:

```bash
python3 tools/run_reasoning_model_benchmark.py \
  --date 2026-08-28 \
  --model qwen3.8-27b-benchmark-4k:latest \
  --base-url http://127.0.0.1:11434/v1 \
  --reasoning-effort low \
  --context-budget-tokens 2000 \
  --max-output-tokens 2048 \
  --output benchmarks/reasoning/runs/model-bound-2026-08-28-qwen3.8-27b-sr2a-current-source.json
```

The OIEC-SR system descriptor for this slice is
`super_reasoning_kernel_four_path_grounded_topology_v2`. Its source-bound
surface includes content-addressed inference edges, explicit inference modes,
finite evidence-node validation, positive grounding traces, typed attack
relations, disconnected-branch rejection, and removal of Qwen3.8's visible
`</think>`-terminated scratch prefix before strict JSON parsing or persistence.

The runner refuses to overwrite an existing artifact or `baseline-v1.json`.
Each new JSON run receives a sibling `.sha256` file and both paths must be new.
It requires an exact model digest and, for local Ollama comparisons, an observed
`100% GPU` allocation with sufficient runtime context. Provider errors become
explicit benchmark results; transport retries remain disabled.

The current development tasks expose standardized evidence handles to exercise
the evidence-provenance wiring. Consequently model-backed runs are labelled
`development_model_plumbing_only`, record that one run is not reproducibility
evidence, and keep `performance_claim_allowed=false`. Raw private reasoning is
not persisted; only answer text, bounded metrics, usage, and sanitized response
hashes enter the artifact.

The initial live probe showed that this local model's `medium` effort exhausted
the 2,048-token output allowance before emitting proposer JSON. The bound
development profile therefore uses `low` effort and records that choice. Batch
reasoning also halts after the first empty or malformed JSON response instead of
spending the remaining proposer or verifier calls on an invalid episode.
The first full run is preserved as failed evidence: the original model's 8K
context checkpoint cache exhausted host/GPU memory and systemd restarted
Ollama. Its inherited template also hardcodes xhigh reasoning regardless of the
requested profile. The compensated model is built from
`models/qwen3.8-27b-benchmark-4k.Modelfile`, removes that hardcoded instruction,
uses a 4,096-token context, a 2,000-token input budget, a 2,048-token output cap,
and a two-step path limit. The runner requires input plus output budgets to fit
inside the observed runtime context.

The neutral 4K Qwen3.8 profile avoided the OOM but still exhausted proposer
output before producing valid JSON. The installed `qwen2.5:14b` model completes
the proposer, verifier, falsifier, and synthesizer request sequence under the
same bounded contracts when the unsupported Responses `reasoning` field is
omitted. The development runner therefore defaults to this exact model with
`reasoning_effort=provider_default`; its digest remains mandatory in the run.

Benchmark task and output schemas are strict: unknown fields fail closed,
task IDs are unique, task files are read in lexical file order and line order,
and every task requires one observation from each system.

Qualification results must use held-out tasks with non-leaking evidence choices
and must be written to a new content-addressed report. They
must never overwrite `baseline-v1.json` or convert fixture metrics into release
evidence.

The first failed and compensated live runs are retained under `runs/`; see
`runs/SR-0B_FAILURE_ANALYSIS.md` for their exact hashes, OOM evidence,
limitations, and the no-performance-claim conclusion.

## Qualification task contract v2

`qualification-v1.jsonl` remains immutable historical benchmark evidence. The
versioned `qualification-v2.jsonl` task contract adds narrow
`hypothesis_label` and `component_label` oracles for scientific A/B selections
and named debugging components. They accept concise canonical forms such as
`B`, `Hypothesis B is better supported`, `loader`, or `The loader is the
earliest supported fault location`, while rejecting negated, ambiguous, or
competing labels.

Generate either version explicitly without rewriting the other:

```bash
python3 tools/build_reasoning_qualification_tasks.py --task-version 1
python3 tools/build_reasoning_qualification_tasks.py --task-version 2
```

## Resumable live qualification

Long direct-Qwen qualification runs use ordered contiguous shards. A shard is
still a complete signed `BenchmarkRun`, but covers only the explicit task range
named by `--task-start` and `--task-count`. Existing artifacts are never
overwritten, so an interrupted qualification resumes by running only missing
ranges.

Provider-bound development checks remain labelled
`development_model_plumbing_only`. Held-out qualification shards must opt into
`held_out_model_qualification_candidate`; this label records evidence purpose
but still leaves `performance_claim_allowed=false` and human review mandatory.

```bash
python3 tools/run_reasoning_model_benchmark.py \
  --date 2026-08-29 \
  --provider llama_cpp_process \
  --task-file tasks/qualification-v2.jsonl \
  --task-start 0 \
  --task-count 10 \
  --output reports/qualification/run-a/shard-000.json \
  ...exact Qwen3.8 and llama.cpp identity arguments...
```

After every expected range exists, merge against the complete task file:

```bash
python3 tools/merge_reasoning_benchmark_shards.py \
  reports/qualification/run-a/shard-*.json \
  --task-file tasks/qualification-v2.jsonl \
  --output reports/qualification/run-a/merged.json
```

The merger rejects overlap, gaps, task-signature drift, source drift, runtime
identity drift, non-contiguous shards, and altered provider profiles. For the
direct provider it aggregates token, call, failure, response-hash, runtime, and
reasoning-certificate telemetry in canonical task order. Fixture shards merge
byte-identically to a monolithic run. Certificate reproducibility is derived
from the repeated runs' persisted OIEC-SR certificate signatures; the
qualification CLI may only assert the derived value, not supply it as evidence.

For multi-day runs, prefer the controller. It freezes one signed source/task/
command manifest, validates every existing shard before resume, preserves one
log per attempt, stops on source or provider/runtime drift, and writes the
merged artifact only after exact coverage:

```bash
python3 tools/run_reasoning_qualification_shards.py \
  --run-dir reports/qualification/qwen38-full-a-20260829 \
  --task-file tasks/qualification-v2.jsonl \
  --shard-size 10 \
  --max-new-shards 1 \
  -- \
  --date 2026-08-29 \
  --provider llama_cpp_process \
  --model qwen3.8-27b-direct \
  ...exact Qwen3.8 and llama.cpp identity arguments...
```

Rerun the same command to advance another shard. Omit `--max-new-shards` to run
all remaining ranges. Any source, task, command, runner, or Git-state change
requires a new run directory rather than rewriting evidence.

The frozen full-SR configuration uses four candidate paths with individual
proposer and falsifier calls plus deterministic two-candidate verifier
micro-batches. Every individual machine-readable role and every base/OIEC
benchmark answer travels through the reviewed compact function-call-only tool
grammar, preventing malformed inner JSON strings before semantic validation.
Tool schemas constrain the top-level vocabulary but do not mark role fields as
transport-required; deterministic role parsers own required semantics and
fail closed or fall back only through their explicit bounded policies.
Batched verifier results use exact positional binding and expand the compact
`failed_checks` wire format into the complete deterministic check map before
scoring. Proposer batching remains disabled because live Qwen3.8 evidence showed
that paired proposer output expanded past the bounded generation budget. The
three role batch sizes are included in the ablation configuration signature.

## Live ablation corpus and manifest

Main release comparison uses all 600 frozen `qualification-v2.jsonl` tasks.
Live ablations use the separate frozen
`qualification-ablation-v1.jsonl` corpus: one deterministic parent-aligned
ten-task block from each required class, sixty tasks total. Its manifest binds
the parent task-set checksum, exact parent shard indexes, selection rule, class
counts, and output checksum. This smaller corpus measures component
sensitivity; it does not replace the 600-task A/B/C performance comparison.

Because each selected block is an exact ten-task shard from the parent corpus,
the `full_sr` ablation may be reconstructed from the corresponding completed
main-run shards instead of repeating identical model work. The current frozen
selection uses parent shard indexes `3`, `11`, `22`, `36`, `47`, and `56`.
Every other ablation must still execute its own exact configuration against the
same sixty tasks.

Run each required ablation through the same shard controller with
`--task-file tasks/qualification-ablation-v1.jsonl` and the corresponding
`--ablation` value. Then bind the real benchmark artifacts into a signed
manifest:

```bash
python3 tools/build_reasoning_ablation_manifest.py \
  --run one_path_only=reports/qualification/abl-one/merged.json \
  --run without_hypothesis_state=reports/qualification/abl-no-hyp/merged.json \
  --run without_verifier=reports/qualification/abl-no-verifier/merged.json \
  --run without_falsifier=reports/qualification/abl-no-falsifier/merged.json \
  --run without_diversity_filter=reports/qualification/abl-no-diversity/merged.json \
  --run without_synthesis_verification=reports/qualification/abl-no-synth-verify/merged.json \
  --run without_adaptive_compute=reports/qualification/abl-no-adaptive/merged.json \
  --run full_sr=reports/qualification/abl-full/merged.json \
  --output reports/qualification/ablation-manifest.json
```

`tools/qualify_reasoning_runs.py` accepts only that signed, checksum-verified
manifest. It rejects arbitrary signature strings, mismatched task sets,
development-labelled runs, source drift, provider/runtime drift, and an
ablation ID whose persisted SR pipeline does not carry the exact expected
configuration signature.
