# SR-0B Provider-Bound Run Analysis

**Analysis date:** August 28, 2026

## Qwen3.8 failed run

Artifact:

```text
model-bound-2026-08-28-qwen3.8-27b-fast-low.json
```

Bindings:

```text
model digest: 07cb98f8840ce491fc28c04a5ecc13c4dec5fd23d9a4732878bc3c02acb5b005
run signature: 23195c60c777deafe9d68daaa7a1393cd48ecd9f90acae7080e6cd7bd122f585
artifact SHA-256: 5a1fbf194327f25ca5e00bbaa339c5ac5de58574fee734b245c46e07294c057c
```

The run is preserved rather than rewritten. The first two tasks produced some
responses, after which Ollama returned connection failures. The user service
was OOM-killed at 21:46:23 AEST and restarted at 21:46:26 AEST. The run records
zero transport retries, the failed observations, the last verified `100% GPU`
allocation, exact token usage for completed responses, and sanitized response
hashes. It does not authorize a performance claim.

The source model used an 8,192-token context and an inherited template that
hardcoded xhigh reasoning. Proposer and verifier responses repeatedly exhausted
their output allowance, and accumulated 8K context checkpoints contributed to
host/GPU memory pressure. A neutral-template 4K derived model removed the OOM
condition but still exhausted proposer output before valid JSON.

## Compensated transport-clean run

Artifact:

```text
model-bound-2026-08-28-qwen2.5-14b-provider-default.json
```

Bindings:

```text
model digest: 7cdf5a0187d5c58cc5d369b255592f7841d1c4696d45a8c8a9489440385b22f6
run signature: c3b69ab4c37f6d5c0b612f3b1a408c94493dc13368c7eb1081f01bb1c044bc1f
artifact SHA-256: 0916bfc7979c9798672c19eff1dce0ab81462d7f4060005ddfa763831abbdac4
```

This run completed all eight tasks and 24 A/B/C observations with zero provider
failures, zero retries, observed `100% GPU` allocation, and exact model,
runtime, source, response, usage, and timing bindings. The base arm made eight
provider calls, governed OIEC made eight, and OIEC-SR made 67 bounded calls.

The run is plumbing evidence, not reasoning-quality evidence. The current
strict exact-answer oracle penalizes explanatory answers, standardized evidence
handles are exposed, and the generic bootstrap hypothesis pool produced no
accepted OIEC-SR survivor on these eight tasks. Those limitations are retained
in the artifact rather than normalized away.

## Current-source confirmation run

Artifact:

```text
model-bound-2026-08-28-qwen2.5-14b-provider-default-current-source.json
```

Bindings:

```text
model digest: 7cdf5a0187d5c58cc5d369b255592f7841d1c4696d45a8c8a9489440385b22f6
source manifest: 91cadc30758e37b383eb214aaceafcd32fbc55823653d543c1774b8e395167d4
run signature: 54f046f7b43d677337da0d98f99d507748badb4bb0948f7961306e6de37a32a5
artifact SHA-256: 87c9560e5cdf3f01c2dc5393873120457501051728c161fca3dcb9e36b2f1085
```

Every source file in the artifact manifest matched the current file SHA-256 at
audit time. The run again completed eight base calls, eight governed OIEC calls,
and 67 OIEC-SR calls with zero provider failures and zero retries. All three
systems were observed at `100% GPU` with an 8,192-token runtime context. The
artifact passed its strict JSON schema and checksum, and it contains no API
credential, authorization header, encrypted reasoning, or private-reasoning
payload markers.

## Conclusion

SR-0B proves that provider-bound A/B/C execution, failure recording, privacy
filtering, append-only artifacts, exact model identity, zero-blind-retry, and
in-call GPU attestation operate end to end. It does not prove that OIEC-SR
improves reasoning. The next implementation slice should replace the generic
bootstrap hypotheses with the first-class bounded `HypothesisSet` and explicit
evidence-bound update records from SR-1A.
