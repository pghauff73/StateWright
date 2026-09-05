# Narrow source-to-function pipeline

The first real-source family is exact numeric conversion from CSS inches to CSS
pixels. The reviewed W3C CSS Values 3 section 5.2 (22 March 2024) specifies the
ratio and preserves its anchoring and device-pixel caveats. The source fixture
is verbatim HTML, not a procedure rewritten into StateWright's fixture grammar.

## Classification and diagnostics

Extractor v4 requires explicit procedure wording or input/output declarations
before staging an ordinary algorithm candidate. Incidental mentions remain
searchable source records with `algorithm_mention` metadata. This conservative
classifier is not a general pseudocode recognizer; missed procedures should be
measured alongside false positives, not discarded from the source corpus.

New fragments report `SOURCE_INPUT_DECLARATION_NOT_PARSED`,
`SOURCE_OUTPUT_DECLARATION_NOT_PARSED`, `SOURCE_PROCEDURE_NOT_PARSED`, and
`UNSUPPORTED_PROCEDURE_SYNTAX_OR_FAMILY`. These do not assert that a source omitted
the information. Old fragments retain their original diagnostic interpretation.
Stored candidates are not rewritten or automatically cleared.

## Section preservation

HTML algorithm/procedure headings group nested content through the next heading
of equal or higher rank. Member text hashes and original byte spans preserve
lineage. Generic sections retain decoded non-executable text; the selected CSS
family retains exact raw section bytes for revision binding. Missing end
boundaries and rejected context block translation. HTML scans at least 4,096
bounded blocks (or the configured larger fragment count), groups sections, then
applies the output limit with complete sections first. Omitted document content
is reported separately; a complete pinned section does not become incomplete
merely because unrelated later material was omitted. Ungrouped fragments from a
truncated extraction remain blocked.
Oversized sections retain bounded fragments marked incomplete, never silently
promote an isolated child procedure. Referenced dependencies remain uninterpreted.
RFC page-oriented HTML and plain-text section reconstruction remain future work.

## Translator and qualification

`css-absolute-length-in-px-v1` accepts only the SHA-256-pinned reviewed section
and complete context. It emits the existing two-node exact scalar affine IR with
slope 96 and bias zero, using semantic names `css_length_in` and `css_length_px`.
Units and the limited applicability are bound into translation verification.
Changed source bytes require another reviewed adapter. This intentionally trades
generalization for an auditable first family.

The existing exact evaluator supplies qualification. Frozen expected outputs
cover negative and positive rational values independently of translator output;
mutated ratios, conditions, missing context and units are negative fixtures.
Fixture partitions are not independent production reviews. Passing fixture
qualification does not authorize live canonical admission or establish probation.
The source is eligible for the existing W3C watch policy, subject to native live
robots, transport, size and license preflight.

## Evaluation gates

- Track classification precision and recall, not merely fewer quarantines.
- Verify complete section boundaries and reject lost conditions.
- Verify the captured real source without rewriting it as a supported procedure.
- Require source-bound exact outputs and failure on mutated evidence.
- Preserve qualification, promotion and probation requirements.
- Do not claim accepted live yield until independent review and actual probation
  evidence exist; report missing prerequisites rather than invent them.

The initial classification fixture covers six incidental mentions/sample calls and four
explicit declaration/procedure examples. This small development corpus is not a
held-out or production accuracy estimate.

## Recorded implementation result (2026-09-05)

- The ten-case development classification corpus passed: six mentions/sample
  calls remained evidence and four declaration/procedure examples were retained
  as candidates. This is a regression result, not measured production precision.
- All 81 focused internet/mathematical-context tests passed, with 807 assertions.
- Three native CLI checks passed: internet lifecycle, watchlist, and all lanes.
- A native one-URL W3C pilot passed preflight, fetch, extraction, and feed. It
  retained 256 fragments and produced exactly one `VALIDATION_READY` candidate,
  with no quarantine reasons. The complete reviewed section was retained even
  though the rest of the document exceeded the output budget. The extraction
  receipt correctly reports truncation of other material.
- Pilot integrity passed and its sole watch was disabled. Accepted and
  probationary algorithm counts are both zero. The source pipeline is working;
  independent production qualification and probation are still outstanding.
- No existing project or 100-URL batch candidates were rewritten. The pilot is
  isolated and its candidate is not a project-root canonical function.

Local evidence: `build/saa-affine-source-FNCmFD/pilot-evidence-v3/`.
Store: `build/saa-affine-source-FNCmFD/pilot-store-v3/`.
Candidate:
`internet-algorithm-candidate:sha256:e47b590c3224a8b1b3dbfda98c0b5785cca7b226c5d06f36b612e0ce4708fcd1`.
Event head:
`f5a6684f4b71ac90e3a2c347fc2c1b033cdd2ddce247eb16b53e4029f35631b3`.

The earlier pilot directories retain the incomplete-context and optional-HTML-tag
failure evidence that motivated the extraction fixes. Production source polling
scope and qualification policy were not expanded by these isolated runs.
