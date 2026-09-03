# SR-1A First-Class Hypothesis State Benchmark Analysis

**Analysis date:** August 28, 2026

## Artifact

```text
model-bound-2026-08-28-qwen2.5-14b-sr1a-current-source.json
```

Bindings:

```text
model digest: 7cdf5a0187d5c58cc5d369b255592f7841d1c4696d45a8c8a9489440385b22f6
source manifest: 4c93486c3b3c3d18ce7809b6d7ca01e559dab1e67c50fbfe51b8ae1226554d15
benchmark ID: 45c34f18947a3341a1c02e9afd7947d426d591febe1050e96fbdf5eacc79352d
run signature: cd57a874cd75914ca91becca0f0632412b35e2445883c227fadaff8b61bf3649
artifact SHA-256: dc8d95bcc9913ef3c1c0a5728fd1a45d0988c8977fabf78b206e3954cd813abf
package version: 0.6.0
```

Every source file in the artifact manifest matched the current checkout at
audit time. The artifact passed its strict schema and checksum, contained no
credential or private-reasoning markers, and recorded zero transport retries.

## Runtime evidence

All three systems used the exact `qwen2.5:14b` digest above and were observed at
`100% GPU` with an 8,192-token runtime context.

```text
base:    8 calls, 0 failures,   3,116 tokens
OIEC:    8 calls, 0 failures,   3,394 tokens
OIEC-SR: 66 calls, 0 failures, 107,721 tokens
```

The OIEC-SR descriptor binds the pipeline as:

```text
super_reasoning_kernel_four_path_hypothesis_state_v1
```

This proves that every path episode began from the signed immutable
`HypothesisSet` owner rather than the legacy dictionary projection.

## Outcome

The OIEC-SR arm returned `INSUFFICIENT_EVIDENCE` for all eight development
tasks. No accepted survivor was produced. Aggregate OIEC-SR correctness
therefore remained zero under the strict exact-answer oracle.

The earlier provider-bound run also produced zero accepted OIEC-SR survivors.
SR-1A consequently establishes no reasoning-quality gain. Its demonstrated
gain is architectural: bounded first-class hypothesis state, deterministic
normalization, evidence-bound update records, RuntimeState v4 replay, and
content-addressed CFEL learning all operate through the live provider path.

The base and governed OIEC scores changed between single runs while the exact
model remained fixed. That variation confirms the existing warning that one
provider run is not reproducibility or performance evidence.

## Conclusion

SR-1A satisfies its state-management exit gate but does not solve candidate
grounding. The next slice should strengthen machine-checkable reasoning
topology before adding more search breadth. SR-2A should add typed inference
IDs and modes, finite evidence-reference validation, and conclusion-grounding
traces so the verifier can distinguish unsupported generic paths from paths
that actually connect the task evidence to a conclusion.
