# SAA Internet Improvement Supervisor Mode

**Implementation status:** Implemented and qualified for the documented
Linux/POSIX fixture scope; the multi-preset result is recorded in
`docs/RELEASE_QUALIFICATION.md`.

## Purpose

`statewright-internet-supervisor` provides a bounded operational control loop
for the SAA persistent internet improvement lifecycle. It is a separate C++20
executable that launches short-lived `statewright internet-improvement` child
processes. The supervisor never opens or edits the EGCF store directly.

The separation is deliberate:

- the supervisor owns process lifetime, timeouts, bounded retries, backoff,
  circuit breaking, signal handling, and JSONL operational events;
- the Director selects deterministic, policy-bounded work;
- the Orchestrator acquires leases, revalidates preconditions, executes at most
  one closed action, and persists terminal receipts;
- EGCF remains the canonical owner of plans, runs, events, leases, receipts,
  source lineage, protocols, and probation history.

Supervisor mode does not add a generic command executor, expand EGCF authority,
or add a human approval step. `internet-improvement approve` remains
unsupported.

## Process Model

One supervisor wake is always bounded by:

- a positive maximum cycle count;
- a positive wall-time limit;
- a timeout for each child process;
- a maximum consecutive-failure count;
- bounded child stdout and stderr files;
- an action lease and fetch lease at least as long as the child timeout.

Each cycle first calls filtered `run-status` for the configured worker. If one
nonterminal leaf run exists, the supervisor either:

- defers while its current immutable action lease remains active; or
- invokes `resume` after the lease expires or when no active lease exists.

If no nonterminal run exists, the supervisor invokes `run-once`. Each child
exits and releases the workspace lock before the next cycle begins.

The supervisor treats a run referenced by a successor's `resume_of_run_id` as
superseded. More than one unresolved leaf run for the same worker is ambiguous
and fails closed through the supervisor circuit.

## Basic Invocation

Use one stable logical worker identity for each workspace and host. Do not use a
PID or a random value because recovery of an active lease requires the same
worker identity.

```bash
workspace=${STATEWRIGHT_WORKSPACE:?set STATEWRIGHT_WORKSPACE}
worker_id=${STATEWRIGHT_WORKER_ID:?set STATEWRIGHT_WORKER_ID}

statewright-internet-supervisor \
  --workspace "$workspace" \
  --worker-id "$worker_id" \
  --maximum-cycles 8 \
  --maximum-failures 3 \
  --maximum-wall-seconds 300 \
  --child-timeout-seconds 120 \
  --action-lease-seconds 180 \
  --fetch-lease-seconds 180 \
  --action-deadline-seconds 120 \
  --success-delay-milliseconds 250 \
  --failure-backoff-initial-milliseconds 5000 \
  --failure-backoff-maximum-milliseconds 60000 \
  --event-log "$workspace/supervisor/internet-events.jsonl"
```

The executable finds a sibling `statewright` binary by default. Use
`--statewright PATH` when the controlled CLI is elsewhere and
`--resource-root PATH` only when normal installed resource discovery is not
appropriate.

`--once` is shorthand for `--maximum-cycles 1`.

## Request Template

Use `--request-file` to supply stable policy fields and immutable IDs. The
supervisor forcibly owns and replaces these protected fields on every child
request:

- `action`;
- `workspace` and `resource_root`;
- `worker_id`;
- `current_timestamp` and interval-derived `cycle_key`;
- action and fetch lease expiries;
- `policy.maximum_actions`, which is always one;
- `policy.action_deadline`;
- recovery `run_id`.

The request file and its canonical JSON representation are each limited to
64 KiB. Child stdout and stderr are independently bounded by
`--maximum-child-output-bytes`, with a default of 1 MiB and a hard ceiling of
64 MiB per stream.

Example template:

```json
{
  "strict": true,
  "policy": {
    "maximum_provider_calls": 1,
    "maximum_response_bytes": 8388608,
    "maximum_cpu_units": 4,
    "maximum_cost_bp": 20000,
    "maximum_risk_bp": 6000,
    "require_reasoning": true,
    "enable_acquisition": true,
    "enable_candidate_advancement": true
  }
}
```

The JSONL start event records only the template's SHA-256 and field names, not
the template contents.

## Result Handling

| Orchestrator status | Supervisor behavior |
| --- | --- |
| `COMPLETED` | Count the action, reset the circuit, delay, and continue within the wake budget. |
| `RECONCILED` | Count recovery, reset the circuit, and continue. |
| `STALE` | Count the stale action, reset the circuit, and replan on the next cycle. |
| `NO_ELIGIBLE_WORK` | Stop the wake successfully. |
| `FAILED` | Count a failure, apply exponential backoff, and open the circuit at the configured ceiling. |
| Active unexpired lease | Emit `RECOVERY_DEFERRED` and stop successfully so a later timer wake can retry. |
| Invalid, timed-out, signalled, or excessive child output | Emit `INVOCATION_FAILED`; a later cycle or wake discovers any durable nonterminal run. |

The supervisor exits zero for successful bounded outcomes, including
`NO_ELIGIBLE_WORK`, `MAXIMUM_CYCLES`, `MAXIMUM_WALL_TIME`,
`DEFERRED_ACTIVE_LEASE`, and graceful `STOP_REQUESTED`. It exits nonzero for an
open circuit or invalid configuration.

## Operational Events

Every line on stdout is canonical JSON using protocol
`statewright-internet-improvement-supervisor-v1`. When `--event-log` is set,
the same lines are appended to an owner-readable and owner-writable file.

Event types are:

- `SUPERVISOR_STARTED`;
- `CYCLE_STARTED`;
- `RECOVERY_SELECTED`;
- `RECOVERY_DEFERRED`;
- `RUN_RESULT`;
- `INVOCATION_FAILED`;
- `SUCCESS_DELAY`;
- `FAILURE_BACKOFF`;
- `SUPERVISOR_STOPPED`.

Child stdout and stderr are never copied into supervisor events. Invocation
metadata records only exit state and SHA-256 digests.

Inspect a completed wake:

```bash
jq -s 'last.details.summary' \
  "$STATEWRIGHT_WORKSPACE/supervisor/internet-events.jsonl"
```

## systemd Timer

The qualified deployment shape is a timer invoking a bounded oneshot service,
not a continuously running daemon.

Example `statewright-internet-supervisor@.service`:

```ini
[Unit]
Description=StateWright internet improvement supervisor wake for %i
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
User=statewright
Group=statewright
StateDirectory=statewright/%i
StateDirectoryMode=0700
ExecStart=/usr/bin/statewright-internet-supervisor --workspace /var/lib/statewright/%i --worker-id internet-%i-host01 --maximum-cycles 8 --maximum-wall-seconds 300 --child-timeout-seconds 120 --action-lease-seconds 180 --fetch-lease-seconds 180 --action-deadline-seconds 120 --event-log /var/lib/statewright/%i/supervisor/internet-events.jsonl
TimeoutStartSec=330
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
```

Example `statewright-internet-supervisor@.timer`:

```ini
[Unit]
Description=Schedule StateWright internet improvement supervisor for %i

[Timer]
OnBootSec=1min
OnUnitInactiveSec=5min
RandomizedDelaySec=30s
Persistent=true
Unit=statewright-internet-supervisor@%i.service

[Install]
WantedBy=timers.target
```

Use only one timer or supervisor for a workspace. Separate workspaces may run in
parallel.

## Safety Boundary

Supervisor mode does not manufacture license classifications, experiment
protocols, benchmark results, probation observations, or promotion policy
outcomes. The native fetch provider evaluates `robots.txt` per origin and binds
the decision and document hash into the fetch receipt. When the active source
policy does not require a known license, the source coordinator may register an
`UNKNOWN`-license assessment input from that receipt automatically. Missing or
failed robots evidence and policies requiring a known license still fail closed.

The EGCF workspace lock is held during each child invocation, including provider
calls. A killed fetch can have reached the remote server before its local receipt
was persisted. Therefore the supported claim remains restart-safe local
reconciliation with at-least-once external HTTP, not exactly-once transport.

## Verification

Run the focused gates:

```bash
cmake --build --preset developer -j2
build/developer/Tests/statewright_contract_tests 'internet supervisor*'
ctest --test-dir build/developer \
  -R statewright_internet_supervisor_smoke --output-on-failure
```

`Tests/internet_supervisor_smoke.sh` verifies the installed-style executable
boundary, exact stdout/event-log equality, a fresh no-work EGCF run, hard child
timeout, child process-group termination, and circuit opening without Python or
live internet.
