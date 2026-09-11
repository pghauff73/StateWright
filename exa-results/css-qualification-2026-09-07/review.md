# CSS conversion qualification: literature review
Date: 2026-09-07
Scope: CSS inches to CSS pixels in the isolated SAA store build/saa-feed-100-20260905-EqQArB/store.

## Conclusion
Measurement evidence is defensible for implementation performance and operational reliability. It is not necessary to empirically establish the CSS conversion constant. Qualification should distinguish semantic correctness, execution correctness, performance, and deployment safety. Treating them as interchangeable either leaves genuine risks untested or creates unnecessary barriers.

This is a targeted narrative review, not a systematic or exhaustive literature review. Exa searches returned 19 result entries, including duplicates, across benchmarking, test oracles, independent reproduction, measurement bias and preregistration. Primary specifications and engineering documentation were also consulted directly. No benchmarks, qualification runs or policy changes were performed for this review.

## 1. What is being qualified?
W3C CSS Values and Units Level 3, section 5.2, defines 1in = 96px. CSS pixels and physical device pixels are different concepts. The specification explicitly permits physical units not to match physical measurements when anchored to the reference pixel. Manufacturer screen or printer calibration is therefore irrelevant to the narrow conversion, but would matter for a separate physical-output claim. [W3C, pinned 2024 draft](https://www.w3.org/TR/2024/CRD-css-values-3-20240322/#absolute-lengths)

Recommendation: qualify the typed mapping CSS-in to CSS-px, f(x)=96x, over an explicitly supported numeric domain. Do not claim physical inches-to-hardware-pixels conversion. Specify representation, input bounds, overflow and error behavior. Mathematical exactness does not automatically prove parsing, serialization or runtime correctness.

## 2. Correctness evidence and the oracle problem
Barr, Harman, McMinn, Shahbaz and Yoo's 2015 survey distinguishes the implementation under test from the oracle that determines its expected behavior. Specifications, formal models and metamorphic relations can automate parts of this judgment. [The Oracle Problem in Software Testing](https://philmcminn.com/publications/barr2015.pdf)

Application: use an independently implemented exact-rational oracle derived from the pinned specification. Check the coefficient and intercept symbolically, then test the actual parsing/execution/serialization path. Example expected values derived from the specification are f(0)=0, f(1/2)=48 and f(1)=96.

Metamorphic properties such as additivity are useful supplements, not sufficient evidence: the incorrect conversion f(x)=95x is also additive. An anchored expected result is essential. Test deliberately incorrect coefficients, offsets and unit labels. For floating-point paths, define rounding and exceptional-value semantics separately rather than demanding exact rational identities indiscriminately.

## 3. What independence means
ACM distinguishes repeating one's own computation from independent reproduction using supplied artifacts and independent replication using separately developed artifacts. [ACM artifact review](https://www.acm.org/publications/policies/artifact-review-and-badging-current)

Application: two seeds or disjoint input groups on one runner do not establish independent teams, implementations or experimental setups. For an automated SAA process, record a narrower, honest independence claim: separately implemented oracle/checker, separate execution context and provenance, with declared shared dependencies. This is not automatically equivalent to ACM's independent-team criterion.

Automation can enforce a predefined evidence contract, but cannot make evidence independent merely by assigning different group IDs. A checker that trusts candidate-generated expected values remains circular.

## 4. Why performance must actually be measured
Kalibera and Jones (2013) show why performance results require repetition at the levels where variation occurs and effect-size confidence intervals. Their work does not supply a universal repetition count for every benchmark. [Rigorous Benchmarking in Reasonable Time](https://kar.kent.ac.uk/33611/45/p63-kaliber.pdf)

Mytkowicz and colleagues (2009) demonstrate that apparently incidental experimental setup choices can bias performance conclusions, motivating setup randomization and causal investigation. [IBM research publication](https://research.ibm.com/publications/producing-wrong-data-without-doing-anything-obviously-wrong)

Application: measure both the conversion kernel and the user-relevant SAA path. Separate cold store startup from warm conversion costs; otherwise store overhead can obscure the operation being evaluated. Interleave baseline and candidate runs, retain raw samples, record compiler/build/host/load context, and report uncertainty. A multiplication so small that timing overhead dominates requires batched inputs and observable outputs.

Google Benchmark supplies repetition, random interleaving and structured result/context output, but a harness alone does not make an experiment valid. [Official user guide](https://google.github.io/benchmark/user_guide.html)

## 5. A defensible baseline
Recommended baseline: an independently written, straightforward implementation with identical conversion semantics, numeric representation and error contract. Compare the complete SAA path against the existing supported path when such a path exists.

Catalogue absence can establish missing capability, but cannot serve as a timing denominator. A no-op, wrong conversion or deliberately slow implementation is not a defensible performance baseline.

For this simple conversion, useful functionality with acceptable overhead may be a better qualification claim than speed superiority. Predefine either a justified non-inferiority margin/resource budget or an improvement hypothesis. An inconclusive result must remain inconclusive; do not redefine success after observing results.

Nosek and colleagues (2018) explain how preregistration separates predictions from post-hoc explanations. This supports freezing outcomes, thresholds, exclusions and analysis before collecting qualification measurements, not inventing scores afterward. [The Preregistration Revolution](https://www.pnas.org/doi/10.1073/pnas.1708274114)

## 6. Longitudinal evidence belongs to operational claims
Google's SRE guidance describes canarying as a limited deployment compared with a control, evaluated using operational signals and integrated rollback decisions. It addresses failures that testing can miss in real environments. [Canarying Releases](https://sre.google/workbook/canarying-releases/)

Application: monitor mismatches, errors, latency, actual selection of the candidate, provenance integrity and rollback behavior. Repeatedly confirming 96x on the same test fixture is not evidence of real use. Small observation counts cannot substantiate strong rare-failure claims.

A potential design is prequalification replay or shadow execution, clearly labelled as such, followed by real probation observations. This is a recommendation, not evidence already collected. If prequalification requires observations obtainable only after admission, the lifecycle has a circular dependency; it needs an explicit non-admitted measurement route, not fabricated probation uses.

## 7. Implications for the recorded SAA blocker
The checkpoint records 16 passing correctness trials, three detected negative controls, a frozen experiment, machine review, missing measurement evidence, and zero probation observations. These are historical recorded results, not independently re-executed findings of this review.

Its recorded diagnostic is:
[json.exception.out_of_range.403] key 'measurement_evidence_id' not found

That diagnostic indicates missing evidence input, not that the CSS formula failed. It does not itself distinguish an absent collector, malformed submission, broken evidence binding or policy/schema mismatch. A focused runtime trace is needed before declaring which is the current implementation cause.

The reviewed literature supports measured performance and staged operational evidence. It does not establish SAA's particular seven-track requirement, fixed group count or fixed observation windows as universal scientific necessities. Their justification must come from explicit system risks and claims. Existing gates remain in force until deliberately revised.

## Recommended qualification package
1. A versioned semantic contract and source binding.
2. Symbolic correctness evidence plus an independent oracle and runtime boundary tests.
3. A fair baseline and preregistered performance/resource criteria.
4. Raw measurements with environment metadata, repetitions and uncertainty.
5. Explicitly labelled prequalification replay/shadow observations, if required.
6. Machine review that validates provenance, independence scope and every required predicate.
7. Separate, real probation observations and automatic rollback criteria.

Expected efficacy: this package can distinguish a correct but expensive implementation from a semantic defect or deployment problem. It cannot guarantee acceptance, establish novelty, replace missing measurements, or make two dependent experiment groups independent.
