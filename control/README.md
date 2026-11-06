# Reference control plane

`agent.py` supervises one Linux AF_PACKET dataplane and offers an authenticated
management API on **127.0.0.1 only**. Python 3.10+ standard library is sufficient;
there are no pip dependencies. The agent does not install tools, manage Docker,
change routes, attach XDP programs or alter kernel settings. Run it only inside
the dedicated Linux lab, with the already-configured NET_RAW capability. Do not
run the raw-packet appliance on macOS or on a production host.

Start with a **new** private state directory, a Linux binary and a validated lab
configuration; paths here are examples inside the lab:

```sh
python3 /tmp/netforge/control/agent.py \
  --binary /tmp/netforge-static --config /tmp/appliance.conf \
  --state /tmp/netforge-agent-run1 --port 9080
```

The agent creates active.conf and a random bearer token under the mode-0700 state
directory; files are mode 0600. It refuses to reuse an existing directory. An agent
owns exactly the child it creates. SIGTERM/INT gracefully stop that child; a child
that fails to stop after three seconds is killed. It does not discover or signal
other appliance processes. Never run two appliances on the same lab interface.
On agent/process restart connection state is lost; reload does not lose it.

## API and publication semantics

Every supported endpoint requires `Authorization: Bearer TOKEN`:

- GET `/v1/status`: running flag, child PID, acknowledged generation, uncertainty.
- GET `/metrics`: request a fresh SIGUSR1 snapshot; Prometheus text format.
- POST `/v1/config`: JSON `{"expected_generation": 6, "config": "version 7\n..."}`.

Config bodies are capped at 64 KiB and HTTP JSON bodies at 80000 bytes. Configs
are validated by the actual engine checker before publication. The serial HTTP
server prevents concurrent publications through this agent. Expected-generation
comparison rejects stale writers. The file is replaced atomically on the same
filesystem with fsync, then only the owned child receives HUP. Success requires
`config_published` for the candidate generation. Runtime rejection restores the
previous file and retains the previously acknowledged generation.

A lost acknowledgement is **uncertain**, not a successful rollback. The engine
might have applied the change. Further publications are blocked; inspect logs and
restart deliberately with the desired config and a new state directory. The agent
has no durable consensus log or automatic crash recovery protocol. Modifying its
private files or signalling its child externally violates its single-writer model.
Only use trusted binaries: checker subprocess stdout is trusted engine output.

The API is for trusted local operators. Bearer auth over loopback is not TLS,
user isolation, or a production-hardened HTTP service. Keep remote nodes behind
SSH tunnels or another separately secured channel. No external listen address is
supported. Request/header timeouts and bounded bodies reduce accidental resource
use; a local process can still disrupt this serial service. Logs and metric lines
are held in bounded memory, not persisted as an audit trail.

## Fleet rollout

`fleet.py` sends updates to up to 16 explicit loopback endpoints, including SSH
forwarded agents. A plan is a JSON list:

```json
[
  {"name":"lab-a", "url":"http://127.0.0.1:9080",
   "token_file":"private/token", "config":"candidate.conf",
   "expected_generation":6}
]
```

Paths resolve relative to the plan. `python3 fleet.py plan PLAN` reads and checks
local plan files and prints names, expected generations and SHA-256 hashes without
network requests or tokens. `python3 fleet.py rollout PLAN` preflights every node,
then publishes sequentially. Each node still compares generations when publishing.
It stops at the first failure and reports already-published nodes. This is a
bounded distributed delivery client, **not distributed consensus or a fleet-wide
atomic transaction**. Fix partial rollouts by preparing a higher generation for
all affected nodes. Reverting behavior requires a new generation, not reuse of an
old version number. Live multi-node validation remains pending.

## Backend draining

Append `/drain` to a service backend token, for example
`10.77.0.130:9000/drain`. Publish a higher generation. New connections skip that
backend; existing mappings retain their original generation and backend until
expiry. If all backends drain, new connections fail closed as
`service_unavailable`. TCP and UDP both support this syntax. Draining is a
configuration policy, not an active health check. There is no automatic probe or
failure detector in this release.

## Verification

`tests/control_test.py` runs the actual C++ config checker and a deterministic
fake child runtime. It checks lifecycle, file modes, stale/invalid candidates,
publication acknowledgements, runtime rejection, uncertainty, HTTP handler auth
and partial fleet failure. It needs no packet capabilities or listening socket.
Real HTTP transport, real dataplane supervision and multi-node delivery still
require Linux integration; these are not proven by the fake-child test.

## Bounded active TCP health proposals

`health.py` runs one observation round against at most 64 TCP backend slots, using
four workers and 50..500 ms connection deadlines (200 ms default). An explicit
`--allow-cidr` scope is mandatory. It validates the source with a trusted NetForge
checker before probes; UDP services are never inferred healthy from TCP behavior.
It tracks consecutive successes/failures (default fall=3/rise=2), and only removes
`/drain` flags it owns for the specific service/backend slot. Manual drains remain.
TCP accept is reachability, not an application-level health check.

```sh
python3 health.py --binary /tmp/netforge-control-static \
  --config accepted.conf --generation 8 --allow-cidr 10.77.0.128/25 \
  --previous-state previous/state.json --output-dir new-round
```

Omit previous-state on the first round. A new private output directory contains
candidate.conf, state.json and report.json. The command never publishes or runs
continuously. Check the candidate with the engine and deliver it through the
acknowledged API/fleet rollout. Carry health state forward with the exact accepted
candidate, including no-change observation rounds; do not pair state with a
rejected or independently edited configuration. Reset state after manual backend
policy edits. Publication/retry coordination remains the operator's responsibility;
this is not an autonomous health/fleet controller. Failed TCP probes consume no
application payload; some application servers log empty accepted connections.

## Acknowledged health reconciliation

`health_controller.py` connects real TCP observations to the agent API. It reads an
authenticated `/v1/config` snapshot, validates it with the real engine, applies the
same explicit address scope/probe limits/hysteresis, and POSTs a higher generation
only when drain/recovery decisions change. Ordinary observation rounds do not
increment configuration generations. Manual drains remain protected per service.

```sh
python3 health_controller.py --url http://127.0.0.1:9080 \
  --token-file /tmp/agent-state/token --binary /tmp/netforge-static \
  --allow-cidr 10.77.0.128/25 --output-dir /tmp/new-health-run \
  --rounds 6 --interval 5 --fall 3 --rise 2
```

This is a finite process, default one round, maximum 20 rounds, 1..30-second
interval with +/-10% jitter, four probe workers and 50..500 ms connection deadlines.
It creates no scheduled job or system service. All output directories are new and
private; previous rounds are not overwritten. `--previous-round DIR` continues
from an accepted round; within a finite run the controller carries state forward.

Each round retains source/candidate configurations and observations. `accepted.json`
is written last only after publication acknowledgement, or a second matching
snapshot for an unchanged decision. It records the accepted configuration hash
and generation. The next round refuses a mismatched hash/generation, preventing
health ownership from being reused across independent manual policy changes.
Reconcile ownership explicitly before restarting after such a change.

A failed/uncertain publication leaves diagnostic artifacts without an acceptance
marker or next-round state. It stops the finite run; it does not repeatedly retry
an ambiguous write or claim rollback. Another operator may have published after
an acknowledged round; subsequent reconciliation detects that drift. The agent
and finite controller are not a distributed transaction, leader election service,
automatic crash-recovery mechanism or application-health checker. Run one health
publisher per agent. Protect the private token/state directories.

The new authenticated GET `/v1/config` returns the acknowledged generation and
config text. It refuses uncertain/stopped dataplanes. It exposes configuration
only to the same authenticated local operator already authorized to publish it.
