# Release Qualification

## Current Status

StateWright is an implementation checkpoint, not an approved production
cutover. The native executable, libraries, immutable stores, deterministic
reasoning kernels, searchable algorithm algebra, governed execution path, and
package smoke tests are available. Human approval and source-owner retirement
are deliberately outside automatic qualification.

The SAA persistent internet improvement lifecycle is a separate knowledge
qualification path. Its deterministic source, experiment, promotion, probation,
and demotion policies require no per-candidate human approval and do not create
or consume approval records. Passing that lifecycle does not approve production
cutover or grant EGCF command execution authority.

As of September 4, 2026, the deterministic Director and bounded Orchestrator
implementation passes the developer, sanitizer, and release presets with 12 of
12 tests in each preset. The qualified orchestration surface persists plans,
runs, run events, action leases, and terminal receipts; executes one closed
typed action per invocation; and keeps `approve` unsupported. This is a
single-process `run-once` qualification, not a production daemon or an
exactly-once HTTP claim.

## Required Gates

1. Run `Tools/verify_release_inputs.sh` against the frozen oracle and packaged
   resource manifests.
2. Configure and build the `developer`, `sanitizer`, and `release` presets.
3. Run the full CTest suite for each applicable preset.
4. Install into an empty prefix and run the packaged CLI smoke test.
5. Generate a new evidence directory with
   `Tools/generate_release_evidence.sh`.
6. Review `docs/RESIDUAL_RISKS.md` and any generated test failures or skipped
   gates.
7. Execute cutover and rollback independently for core, reasoning, SAA, and
   EGCF ownership under an approved migration procedure.
8. Record the authorized human decision without changing historical evidence.

For the SAA internet subsystem, also generate the non-overwriting source-bound
bundle with `Tools/generate_internet_release_evidence.sh`. It records autonomous
promotion and demotion, absence of SAA approval dependencies, no live-internet
or Python requirement, unchanged EGCF authority, migration identities, and the
packaged fixture result. It also runs
`Tests/saa_internet_howto_smoke.sh`, copies the operational HOWTO into the
bundle, and records `howto_validated: true`. This subsystem evidence is
qualification evidence, not production cutover approval.

The internet bundle also runs
`Tests/internet_orchestrator_cli_smoke.sh` and
`Tests/internet_orchestrator_fault_smoke.sh`, and copies the Director and
Orchestrator implementation plan and goal prompt into the evidence directory.

## Authority Boundary

Models and provider adapters may propose hypotheses, claims, evidence requests,
or candidate algorithms. They cannot approve plans, qualify algorithms, promote
knowledge, mutate the workspace, execute cutover, or declare release success.

The only admitted filesystem mutation is the exact-plan C3 transaction path.
All other frozen C0-C2 catalog commands use specialized deterministic handlers
or a generic read-only semantic adapter. Unimplemented C3/C5 commands have no
qualified executable algorithm and fail closed.

General EGCF execution approval remains unchanged. In particular, the exact-plan
C3 approval mechanism is not used by, inherited by, or automatically generated
from internet source acquisition or autonomous SAA knowledge promotion.
