# Remaining NetForge roadmap — 2026-10-20

The original end-to-end scope remains in force. Eight implementation/acceptance
packages remain; these are substantial workstreams, not eight shell commands.
No completion percentage or deadline is inferred from their count. STATUS.md
and VERIFICATION.md describe current evidence.

## Progress reconciliation — 2026-11-06

The eight sections below retain the original workstream scope; their bullets are
not all still pending. Completed subgates since this list was first written:

- XDP: owned replacement/rollback, kernel counters, authenticated API generations,
  audit, rejected-update continuity and verified detach.
- DPDK: actual ring PMD, partial-TX ownership, common-policy parity and bounded
  external PCAP processing/shutdown.
- Workers: UDP/TCP/service/related-ICMP ownership, TSan, transactional reload, idle
  expiry, real AF_PACKET traffic and authenticated API integration.
- Recovery: durable reference-agent checkpoints and exact-child recovery; local
  fleet journal, partial rollout and lost-response reconciliation.
- Hardening: persistent TCP direct/service binary transfers and half-close/EOF.
- Observability: bounded authenticated publication audit and XDP kernel counters.
- Release foundation: actual CMake Release build and17 CTest tests passed before
  the latest XDP API/helper changes; targeted newer gates passed separately.

Since reconciliation: portable XDP interchange/conversion passed Linux; explicit
exception contracts are documented; first16 reference ACL checks/hits and offline
snapshot view passed tests/live checks; archive reproduction passed18 tests;
worker dispatch comparison found no speedup under the CPU cap.

Still open: broader XDP operational recovery; remaining DPDK service
integration appropriate to its declared subset; worker failure/scaling closure;
controller outage/rejoin and remote-management boundary; broader fault matrix;
per-rule/backend/health observability and lightweight view; equivalent-policy
comparative experiments/report; reproducible setup, CI, final regression and
release packaging/documentation. Physical NIC/NUMA studies require other hardware.
This list must not be interpreted as eight remaining commands or eight equally
sized tasks. No end-to-end completion claim has been made.

## Verified foundation

Reference C++ router/ACL, UDP/TCP tracking/SNAT/service translation, related ICMP,
four selection policies, dynamic ARP, bounded queues, local authenticated control
and acknowledged reload/rejection, health drain/recovery and metrics all have
Linux/live evidence. Indexed flow lookup has differential tests and offline
counterbalanced results, including a one-flow regression. Initial stateless XDP
has 312 kernel tests plus config-driven generic-veth packet parity/drop/detach
acceptance. Consolidated twelve-group Linux sanitizer regression passed.

## 1. Complete XDP policy lifecycle and operational integration (phase 11)

- Extend the restricted UDP/static-neighbor compiler deliberately, with explicit
  unsupported-feature rejection and a versioned portable policy artifact.
- Integrate desired/applied revisions and management API validation; implement
  safe immutable publication/replacement and rollback on rejected updates.
- Define downstream exception handling: PASS does not imply full firewall/NAT
  semantics, nor does the current backend accelerate existing stateful flows.
- Add kernel decision/drop counters, bounded audit correlation and lifecycle tests.
- Gate: actual packets before/after accepted and rejected changes, coherent policy
  generations and safe ownership-aware detach. Native-driver NIC performance is
  a separate hardware-dependent measurement.

## 2. Implement and verify DPDK backend (phase 12)

- Inventory/simulate dependencies and choose an installation/build footprint
  compatible with the existing builder and disk guard. No blind toolchain install.
- Implement real EAL/runtime initialization, bounded mbuf/mempool ownership,
  virtual input/output, batching, partial-send/drop and shutdown handling.
- Start with a virtual/PCAP path and the common reference-policy subset; validate
  actual DPDK runtime and byte/action parity. Do not label standalone C++ as DPDK.
- Gate: actual build/runtime, independent output checks, bounded memory and clean
  shutdown. Physical NIC binding/host hugepage changes are outside this Mac lab.

## 3. Safe multicore state ownership (phase 15)

- Specify canonical bidirectional flow ownership across original/NAT tuples.
- Add bounded worker queues, backpressure, ownership-safe NAT allocation/expiry
  and coherent configuration publication. Define worker-failure behavior.
- Gate: differential functional and race tests, no duplicate mappings/ports,
  correct return traffic and a measured one-vs-two-worker comparison. The two-CPU
  VM cannot establish four/eight-core or NUMA scaling claims.

## 4. Durable local and multi-agent control recovery (phases 10, 16)

- Persist desired/applied publication and rollout journals across crash windows.
- Reconcile write/signal/ack uncertainty without misreporting success or reverting
  newer revisions; retry/idempotency and stale-writer protection.
- Live multi-agent partial rollout, outage, restart, rejoin and convergence. Add
  explicit node identity/membership/heartbeat and scoped secure remote channels
  if remote management is supported. Multiple local agents are not multiple hosts.
- Gate: real agents converge after controlled failures and accurately report
  partial/uncertain status. Flow-state failover and consensus are separate claims.

## 5. Correctness, security and bounded failure closure (phases 7–9, 18–19, 23)

- Long-lived TCP data/retransmission/half-close/reset/reload and specified sequence/
  window validation boundaries; bursts, expiry, collision and NAT-port pressure.
- Broader related-ICMP errors/quotes/MTU/malformed/expired cases; decide general
  static DNAT beyond existing service translation.
- Health flapping/all-backend failure, process/controller/worker interruption and
  bounded corrupt/stale-config, queue exhaustion, loss/reordering scenarios.
- Gate: supported behavior passes packet-level tests; recovery/loss is measured;
  state bounds hold. Restarts currently lose flows; no seamless-failover claim.

## 6. Finish operational observability (phase 17)

- Bounded per-rule/backend/config/health accounting, audit/request/revision
  correlation, kernel/socket drop provenance and pressure visibility.
- Lightweight dashboard/export and bounded packet captures demonstrating faults.
- Gate: known failures are diagnosable using actual metrics/logs; wire counters,
  core reprocessing, process memory and delivery/latency remain distinguished.

## 7. Common-feature comparative experiments/report (phases 13–14, 20–22, 25)

- Independent traffic generation and RX/TX/loss/latency verification; compare
  reference/raw-socket/XDP/DPDK only on equivalent supported policies.
- Vary sizes/flow counts/load, optimized builds, repetitions and confidence
  intervals; retain raw data, exact environment and negative results.
- Profile actual bottlenecks before further route/ACL/buffer/batching changes.
- Gate: reproducible network results and systems engineering report. Offline
  core rates are not Mpps on a NIC. Physical NIC/NUMA/cycle studies need suitable
  isolated Linux hardware. Novelty/paper publication remains conditional.

## 8. Reproducible release and interview handoff (phases 0, 4, 24, 26)

- Fresh source/lab setup with ownership labels, safe collision checks and limits;
  resume/stop/approved cleanup procedures and bounded artifact retention.
- Actual CMake/CTest and hosted CI execution, clean reproduction and one bounded
  acceptance/demo entry point. No publication is implied without user direction.
- Synchronize docs/capability matrix/version/archive/source manifest and licenses.
- Gate: another operator can reproduce the supported system without terminal
  history. Resume bullets cite only verified implementation and measurements.

## Execution strategy and boundaries

Next: close the remaining fault/operational contracts, consolidate experiments,
then complete reproducible release and handoff. Implement in coherent batches;
run focused tests per change and full regression at integration boundaries.
Prepare source changes autonomously, while retaining the explicit manual-Docker
output gate. Heavy backend work is serial on this laptop.

VM remains two CPUs/about 2 GiB; builder 512 MiB/.5 CPU/128 PIDs and lab/checkers
128 MiB/.5 CPU/64 PIDs, no swap. Stop dependency/build expansion under 10 GiB
free. Ask before deleting containers/images/volumes/data. No macOS network/kernel
settings, host NIC binding, hugepages or unrelated resources are modified.

Foundational learning (original phases 1–3) continues through packet walkthroughs,
OS/cache/DMA/queue explanations and experiments. Assistant-authored code/docs
cannot certify the user's understanding or interview mastery. Full completion
requires the applicable engineering gates above; a smaller release milestone
will not be renamed full original-roadmap completion.
