> Latest update: bounded captured-source resumption is now enabled after two successful supervisor actions and a passing native integrity check. This supersedes the runtime-pause checkpoint below; live polling remains deferred. See SAA_BOUNDED_INGESTION.md for evidence and remaining work.

> Implementation update (2026-09-06): the v5 binaries now build, 85 focused tests (860 assertions) and seven CLI/supervisor smoke tests pass. Earlier unbuilt/unvalidated statements below describe the pre-implementation checkpoint. Grounded protocol details and trust requirements are in SAA_GROUNDED_EXPERIMENT_PROTOCOL_V2.md. The CSS pilot migration is complete; larger-store feeding was stopped for runtime, and recurring rollout remains paused.

# SAA pipeline repair v5

## Implementation status

These changes are not built or validated yet. Do not claim deployed fixes,
successful migration, qualification, or newly accepted functions from this file.
Old snapshots, candidates and admissions must remain immutable.

## Source eligibility

`internet-improvement` action `readiness` returns a review queue for current
watch registrations whose license status is not verified. Review the exact
source, applicable notices, publisher terms, scope and provenance before using
the existing reviewed-source registration and preflight path. The queue is not
an approval. Metadata licensing does not automatically cover article full text.
The existing 40 blocked sources must remain disabled until their review is real.

## Acquisition

The blocked XML canonicalization snapshot explicitly declares ISO-8859-1 in its
XML prolog. The extractor and source assessor now share a strict decoder for
that declaration on XHTML. No guessed fallback, lossy replacement or remote
entity expansion is used. UTF-8 remains the default. Unsupported declarations
remain blocked. Original bytes and hashes are preserved; decoded text has its
own digest, and fragment/member offsets are mapped back to original bytes.
Other legacy encodings and HTTP-only charset declarations are not implemented.

## Classification and context

Extractor v5 preserves explicit multi-line declaration paragraphs and numbered
RFC top-level sections, including subsections. Consecutive RFC HTML preformatted
pages are grouped with raw member spans and separate decoded section offsets.
Oversized sections remain incomplete evidence, rather than orphaned executable
procedures. Output budgets still apply. Referenced definitions and dependencies
remain uninterpreted. This is bounded reconstruction, not general RFC parsing.

Reprocessing creates new versioned extraction/feed evidence. It does not delete,
rewrite, or relabel the old 520 quarantined candidates. Historical counts must
not be compared directly with new-version precision or yield. Measure reviewed
true positives, false positives and missed procedures in a held-out corpus.

## Translation

New-version explicitly declared scalar procedures support exact decimal and
rational arithmetic, parentheses, constant division and unary signs. Expressions
must reduce structurally to a nonconstant affine function of one declared input.
Unknown symbols, conditions, calls, nonlinear products and variable denominators
are rejected. Parsing has byte, numeric-digit and nesting bounds. Old fragments
retain their original translation behavior. Multi-line declarations may use
singular input/output labels; extra unlabeled statements are not discarded.

The existing pinned W3C CSS Values 3 section now also supplies centimeters,
millimeters, quarter-millimeters, picas and points to CSS pixels. Each retains the
whole reviewed section, a distinct semantic input unit and an exact rational
factor. These are candidate mappings, not five accepted functions or five URLs.
No support is implied for device pixels, arbitrary CSS revisions, or general
programming-language translation.

Source: https://www.w3.org/TR/2024/CRD-css-values-3-20240322/#absolute-lengths

## Qualification and promotion

Registering the shipped promotion policy is supported by the existing native
`internet-promotion-policy` action `register-default`. Select its returned ID in
the supervisor request policy; existence alone does not select it.

Use `readiness` to surface missing configuration separately from source failures.
A genuine experiment protocol still needs a defensible baseline, source-bound
expected results, independent experiment groups and review. Do not manufacture a
constant-wrong baseline to obtain an improvement score, label fixture partitions
as independent reviews, or fabricate observed probation usage.

## Recurring operation

The supervisor captures stdout and stderr through separate nonblocking pipes.
It keeps at most the configured bytes per stream and terminates overflowing
children while preserving timeout, cancellation and process-group handling.
It no longer sets process-wide RLIMIT_FSIZE from the output budget. Inherited
host resource limits remain untouched.

## Controlled rollout, after build and validation approval

1. Build the CLI and supervisor; run focused decoding, extraction, translation,
   replay, large-store and noisy-child checks. Existing tests have not been
   modified as part of this repair and may need new-version expectations.
2. Register/select the existing promotion policy and collect native readiness.
3. Use `internet-improvement` action `reprocess` with an explicit historical
   `policy_assessment_id` and `maximum_fragments: 256`, one snapshot at a time.
   Start with the CSS pilot and the encoding-blocked snapshot. The operation
   re-assesses captured evidence and cannot override other source-policy denials.
   It does not refresh live robots permission or source age for admission.
4. Record new extraction/candidate IDs alongside old IDs. Do not restart the
   one-time feeder. A replay of the same extraction reuses durable dispositions.
5. Review the remaining license queue and obtain genuine qualification inputs.
6. Resume bounded native supervisor cycles and remaining watch initialization;
   report only receipt-backed admissions and material blockers.

## Controlled rollout evidence, 2026-09-06

The repaired CLI, supervisor and contract-test targets built successfully. The focused run passed 85 test cases / 860 assertions, and all seven selected native CLI and supervisor smoke tests passed. This includes a child writing a 2 MiB store file under a 1 KiB output cap, and rejection of a child exceeding that output cap. Logs are `build/saa-v5-focused-tests-3.log` and `build/saa-v5-cli-tests.log`.

Grounded-protocol enforcement is enabled in the isolated 100-URL store and CSS pilot only. Both trust configurations intentionally contain no reviewer keys. Legacy experiment compatibility elsewhere is not a claim of grounded enforcement everywhere. Production reviewer identities, signatures, measurements and probation observations have not been fabricated.

The CSS pilot reprocessing completed with six new validation-ready candidates: in, cm, mm, Q, pc and pt to CSS px. Each has no unresolved translation assumptions. Its native integrity check passed with seven stored candidate records total, including the historical candidate, and zero probation observations. Six newly produced records are not six accepted functions. The overall document receipt remains truncated under the fragment budget; the pinned conversion section is retained. Evidence: `build/saa-v5-pilot-reprocess.json` and `build/saa-v5-pilot-integrity.json`.

An unsigned author handoff is available at `build/saa-v5-css-review-packet.json`. It identifies actual candidate records and source bindings, but deliberately leaves baseline, measurement and independent-review evidence pending. It is not a registered protocol or an approval receipt. See `SAA_GROUNDED_EXPERIMENT_PROTOCOL_V2.md` for the grounded protocol and reviewer trust workflow.

The explicitly Latin-1-declared XML canonicalization snapshot has a new SOURCE_ADMISSIBLE assessment alongside its immutable historical encoding rejection. Its larger-store extraction migration is slower than the small pilot. Complete this bounded replay before expanding to RFC samples or a full batch. Source admissibility does not establish translatability, correctness or acceptance.

Remaining operational work is staged rather than silently marked complete: finish the current large-store migration, verify native integrity, perform a bounded repaired-supervisor run, then restore pending watch initialization using current watch identities and existing policy limits. Independent reviewer onboarding and real frozen experiment evidence remain prerequisites for acceptance; license-review sources remain blocked until legitimately reviewed.

The native capability-baseline action subsequently captured an empty canonical-catalog search for the CSS cm candidate. Evidence is in `build/saa-v5-css-cm-baseline.json`, and the unsigned packet now references its real evidence ID. This baseline applies only to that candidate and pilot catalogue; it is not a fabricated failing implementation, an independent oracle, or a speed claim.

### Final rollout checkpoint: partial, not fully resumed

The large-store replay remained active after more than nine minutes, beyond the existing 120-second recurring child budget, and was terminated by the operator. Its new source assessment and extraction receipt are durable, but the feed did not return a completion receipt. The evidence file `build/saa-feed-100-20260905-EqQArB/supervisor/encoding-partial-migration-v5.json` records this explicitly. The empty interrupted command-output file is not success evidence.

The subsequent native integrity check passed: 60 snapshots, 520 historical quarantined candidates, and zero probation observations. Evidence: `build/saa-feed-100-20260905-EqQArB/supervisor/post-migration-integrity-v5.json`. The checkpoint now distinguishes this newly observed throughput blocker from the repaired process-wide output-limit bug. It preserves pending watch identities and leaves recurring rollout paused.

Next implementation step: diagnose large-store feed runtime and implement bounded, resumable ingestion while preserving complete source context. Then complete the stopped migration, exercise the repaired supervisor against the real store under its resource budget, and restore pending watches. No RFC bulk migration or full recurring resumption was completed in this rollout. Independent experiment reviews and real measurement evidence remain required. New accepted functions: zero; six new CSS conversion candidates are validation-ready in the isolated pilot.
