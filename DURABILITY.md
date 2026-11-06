# Opt-in durable single-agent recovery

`control/durable_agent.py` extends the existing Agent/HTTP handler without
changing the original agent's default behavior. Native and Linux clean-restart, locking, integrity, write-failure, lost-ACK,
exact-orphan pidfd and reused-PID tests passed. Real HTTP/dataplane recovery
passed durable-batch.sh on 2026-10-20, including generations 7, 8 and 10. This is configuration recovery,
not NAT/session-state failover or distributed consensus.

## Persisted contract

A dedicated UID-owned mode0700 root contains a mode0600 bearer token and an
atomic/fsynced bounded journal. Journal SHA-256 covers the record; the checkpoint
configuration also has its own digest. This detects accidental corruption, not
malicious edits by the trusted same UID. Binary SHA-256 must match on resume.
Files are read without following final symlinks. A non-inherited exclusive flock
prevents concurrent writers/supervisors for one state root.

The journal stores schema, binary fingerprint, publication phase, revision high
water, exact child PID/boot ID/start ticks, candidate metadata and base64 checkpoint
configuration. Initially the checkpoint is a validated bootstrap, not an applied
claim. Only a matching runtime started/config_published event followed by journal
fsync commits it as acknowledged. Publication intent is durable before active
file replacement/SIGHUP; successful HTTP return occurs after checkpoint durability.

Runtime rejection restores the previous active file and checkpoint. Its proposed
revision remains consumed. Clients must inspect durable_high_water and propose
a higher revision, while expected_generation still names the applied revision.
Durable I/O failure or missing ACK blocks further publication as uncertain.

## Recovery and child ownership

Fresh deployment refuses an existing state root. Explicit --resume loads the
acknowledged checkpoint (or validated bootstrap if startup never acknowledged),
validates it and constructs a generation strictly above all recorded attempts.
It restores checkpoint semantics with that higher generation and starts a fresh
dataplane; only a fresh matching ACK makes the new instance available.

If the recorded old child is still running, --resume alone refuses. Explicit
--stop-owned-child uses Linux pidfd, rechecks exact boot/start/PID identity, then
sends TERM and, if required after3s, KILL, waiting up to2s further. A reused PID
is never signaled. The private deployment root and same-namespace process record
are trusted ownership data, not a general host process-management facility.

Launch intent is saved before spawning. A crash before PID identity persistence
is an explicit unknown-launch window: recovery refuses a second instance and
requires operator reconciliation. It never guesses which process to stop.
Unreadable identity data also fails closed. Safe orphan termination requires
Linux pidfd support; native macOS coverage is limited to clean restart.

On clean close, the managed child stops and the writer lock is released. State
is retained, not automatically deleted. A root retains at most16 runtime instance
directories, each with bounded configuration/token files; reaching the bound
requires operator review. Original runtime output/metrics queues remain bounded.

## Invocation inside an isolated Linux lab

Fresh: python3 durable_agent.py --binary ENGINE --config CONFIG --state NEW_ROOT
Resume: python3 durable_agent.py --binary ENGINE --state ROOT --resume
Orphan recovery additionally requires --stop-owned-child. HTTP remains loopback
only and bearer-authenticated, with the original request/config bounds.

Existing health/fleet clients are not automatically switched to this opt-in mode;
they need high-water-aware retries. Live distributed rollout journals, portable
binary migrations, complete automatic unknown-launch reconciliation, container
restart reproduction and durable flow-state recovery remain separate gates.

## Acceptance batch

durable-batch.sh uses only the existing builder and live containers and verifies
their limits. Builder tests use the real C++ config checker and a controlled
runtime double to exercise lost ACK, killed supervisor, exact-child recovery,
reused PID safety, integrity failure and exclusive writers. Live tests start
owned loopback agents around the actual C++ AF_PACKET engine, acknowledge7, kill
the owned supervisor, recover8 with retained credentials/config, reject stale
and immutable updates, restart10 and inspect real metrics. All owned test
processes are shut down and private test state is retained. Existing topology,
container lifetimes, Docker settings, host interfaces and dependencies are unchanged.
