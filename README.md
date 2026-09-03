# StateWright

StateWright is a C++20 implementation checkpoint for a governed engineering
command fabric. It unifies deterministic runtime control, OIEC-SR advisory
reasoning and hypothesis construction, EGCF lifecycle/approval machinery, and a
searchable algebra of algorithms behind one native executable.

The current build is non-authoritative. Production ownership cutover and release
approval remain explicit human decisions.

## Build

```bash
cmake --preset developer
cmake --build --preset developer -j2
ctest --preset developer --output-on-failure
```

Additional presets are available for `sanitizer`, `coverage`, and `release`.

## CLI

The stable native protocol accepts one JSON object per operation:

```bash
statewright inspect '{"workspace":".","path":"Core"}'
statewright reason '{"workspace":".","input":{"text":"Why did parsing regress?","hypotheses":["parser defect","fixture defect"]}}'
statewright algorithm '{"workspace":".","action":"list"}'
statewright command '{"workspace":".","command_id":"repo.metrics@1","inputs":{}}'
statewright ledger-verify '{"workspace":"."}'
```

Use `@request.json` instead of inline JSON to read a request from a file. Every
new operation returns a `statewright.cli.v1` JSON envelope. The default authority
is read-only with a C2 ceiling; C3 execution requires an explicit authority and
an immutable human approval bound to the exact plan.

Run `statewright` without arguments for the operation list. Legacy canonical
JSON, hash, typed-ID, hypothesis, SAA, and command-description operations remain
available for migration compatibility.

## Resources

Build-tree binaries verify the source resource manifest. Installed binaries
resolve the packaged bundle relative to their install prefix. An explicit
request `resource_root` or `STATEWRIGHT_RESOURCE_ROOT` environment variable may
select another exact manifest-bound bundle.

## Release Evidence

```bash
Tools/verify_release_inputs.sh
Tools/generate_release_evidence.sh release build/release-evidence/release-1
Tools/generate_internet_release_evidence.sh \
  build/release-evidence/internet-release-2026-09-03
```

The evidence generator refuses to overwrite an existing bundle. It records
source, dependency, resource, test, package, cutover, and human-approval status;
it does not grant release authority.

The dedicated internet bundle records the approval-free SAA knowledge lifecycle,
local fixture qualification, autonomous promotion and demotion, migrations, and
the unchanged EGCF execution-authority boundary.

## Operational Guides

- `docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md` provides the source-grounded
  build, fixture, manual lifecycle, inspection, troubleshooting, bounded
  real-internet, integrity, and release-evidence procedures.

## Implementation Plans

- `IMPLEMENTATION_PLAN.md` records the completed native StateWright refactor
  plan and qualification boundaries.
- `docs/SAA_PERSISTENT_INTERNET_IMPROVEMENT_PLAN.md` records the implemented
  persistent internet acquisition, autonomous approval-free SAA promotion,
  probation, automatic demotion, and qualification boundaries.
