# Acceptance and resume status

Updated 2026-11-06. The full original roadmap is **not complete**. NetForge has
a verified reference appliance, local control plane and an initial stateless
generic-XDP backend. It is not a production firewall or a measured physical-NIC
high-performance dataplane. VERIFICATION.md contains the detailed evidence.

## Verified in Linux / live Docker lab

- C++20 Ethernet/IPv4 checks, routing, ordered ACLs, UDP/TCP tracking and SNAT/PAT,
  reverse service translation, flow affinity and selected related ICMP reverse NAT.
- Local/routed ICMP; real UDP/TCP traffic; reload and rejection continuity.
- Round robin, weighted round robin, least tracked connections and rendezvous
  policies; dynamic solicited ARP with bounded pending queues.
- Authenticated loopback API, acknowledged configuration publication, runtime
  rejection/file restoration, stale-writer protection and metric snapshots.
- Backend draining and bounded TCP health reconciliation through the actual API,
  hysteresis, recovery and external-configuration drift protection.
- Per-port wire counters, core/reprocessing accounting and process resource metrics.
- Optional indexed flow lookups: dual-path tests and counterbalanced offline
  comparison. Warm 64-byte paired indexed/linear rates were 0.905, 2.043 and
  5.084 at 1, 64 and 256 flows. These are offline core trials, not network rates.
- XDP: actual kernel verifier acceptance and 312 synthetic kernel test-runs.
- Generic XDP: actual private-veth redirects with native byte parity, interleaved
  ACL/checksum-negative observations, conditional owned-program detach verification.
- Restricted XDP compiler uses the shared reference parser: real project config
  compiled with namespace-local interface bindings and passed live forwarding
  acceptance on 2026-10-20. Unsupported NAT/stateful/config features fail closed
  at compilation. Policy is one frozen target-native map, not dynamic API reload.

## Current regression gate

The consolidated fifteen-group Linux ASan/UBSan regression and targeted worker
ThreadSanitizer run passed on 2026-10-20 in workers-idle-batch.sh. Real private-veth
UDP/TCP acceptance also verified idle expiry of all twenty mappings. Subsequent worker reload passed the full fifteen-group Linux regression/TSan
and real live generation2 publication, generation3 rejection and continuity. Release CMake/CTest also passed all15 tests on 2026-11-06. Hosted CI and clean fresh-machine setup remain pending.

## Remaining original roadmap

- General XDP policy/API integration, transactional updates/rollback, observability
  and real network experiments; native-driver/NIC acceptance needs suitable Linux.
- DPDK 23.11.4 ring-PMD initial build/runtime, 312-case corpus, partial TX and
  mbuf reclamation passed. Config compilation and nineteen independent C++ reference comparisons also
  passed. General DPDK service lifecycle remains pending. A bounded UDP/SNAT
  WorkerPool passed native/Linux sanitizer and ThreadSanitizer checks.
  AF_PACKET UDP worker dispatch passed sixteen live round trips. TCP/service/related-ICMP ownership passed native/Linux sanitizers and TSan.
  Live UDP and four TCP service/FIN frame exchanges passed, TX56/failures0.
  Related ICMP worker ownership currently has native/Linux coverage.
- Broader distributed recovery beyond verified two-logical-agent rollout/reconciliation.
- Additional bounded security, chaos, sustained traffic and failure acceptance.
- Reproducible setup, release artifacts, CI execution, coherent final documentation
  and comparative reports. Research/paper claims depend on results.

TCP state is bounded connection tracking, not a complete sequence-window firewall.
XDP supports a restricted stateless subset and punts exceptions; it does not
provide the userspace NAT/LB semantics. Generic-veth results do not establish
physical-NIC throughput, multicore scale or production readiness.

Docker execution remains manual. The VM stays at 2 CPUs/about 2 GiB; builder
512 MiB/.5 CPU/128 PIDs, lab/checkers 128 MiB/.5 CPU/64 PIDs, no swap. Existing
lab containers are retained. No macOS interfaces/settings or unrelated data
are modified; deletion requires user approval.

## Durable control extension

Opt-in DurableAgent passed Linux lock/integrity/write-failure, lost-ACK, exact-orphan
pidfd and PID-reuse checks. Real HTTP/dataplane crash recovery, stable credentials,
checkpoint restoration and monotonic restart passed durable-batch.sh on 2026-10-20. See DURABILITY.md. It does not preserve
flow state or complete distributed recovery. Future full suite has fifteen
logical groups; the prior thirteen-group baseline remains the verified full run.

Journaled fleet publication is implemented with local transport-double coverage;
real two-logical-agent HTTP acceptance passed fleet-batch.sh on 2026-10-20.
The next full suite includes fifteen logical groups; targeted passes do not
replace a full consolidated final release run.

Worker reload staging/publication passed Linux/TSan and live acceptance on
2026-10-20. XDP expected-owner replacement/rollback and safe detach passed live acceptance
on 2026-11-06. Kernel decision counters passed the combined312-case kernel/live lifecycle
acceptance on 2026-11-06. API-managed lifecycle/metrics remain unfinished. See WORKERS.md for resource and feature bounds.

A serial Release CMake/CTest acceptance batch is prepared in
netforge-live/cmake-release-batch.sh. It uses only the existing capped builder;
optional CMake/Make dependencies are constrained to an explicit small package
allowlist, no upgrades/removals,128 MiB gross install/64 MiB download bounds.
CMake3.28.3/GCC13.3 Release build and all15 CTest tests passed on 2026-11-06.

WorkerAgent opt-in API integration and check-workers preflight are implemented.
Native sanitizer/adapter checks are tracked in WORKERS.md; the expanded16-test CMake and real private-veth HTTP gate passed on
2026-11-06 in worker-api-20261106-103705-14835. Durable
worker recovery is not provided by this opt-in adapter.

Bounded in-memory publication audit/correlation is implemented; native handler
checks passed. Real HTTP/worker audit acceptance passed audit-batch.sh on 2026-11-06.
The future full CMake suite includes17 tests; the verified full run remains16.

A bounded real DPDK PCAP adapter is implemented with separate routed/punted
outputs, size/count bounds, exclusive outputs, batch mbuf ownership and controlled
shutdown. Native PCAP I/O sanitizer checks passed. The actual DPDK/reference PCAP gate passed checksum-reference-batch.sh on
2026-11-06. It is not a live NIC service or API backend.

The first real DPDK PCAP gate exposed an untranslated UDP-zero-checksum parity
mismatch. Reference preservation is repaired and native sanitizer regression
passed; actual DPDK PCAP acceptance passed checksum-reference-batch.sh,
which rebuilds/tests the reference before using its archive. Older cached core
archives are intentionally refused by source-equality checks.

Latest full Release gate: all17 CTest tests passed in checksum-20261106-105900-17225.
Subsequent shared XDP compiler/native management engine changes have native
compiler/control compatibility coverage; actual XDP HTTP lifecycle/counters/audit
acceptance is pending xdp-api-batch.sh.


XDP HTTP acceptance passed on 2026-11-06 in
netforge-live/xdp-api-20261106-111609-18440: authenticated startup, second-owner
refusal, generation2 deny, higher-revision restore, runtime/preflight/stale
rejection, continued byte parity, program-labelled counters, bounded audit and
owned detach with verified absence. This supersedes the pending API gate above.
No durable XDP recovery or physical NIC performance is claimed.


Persistent TCP direct SNAT/service translation passed on2026-11-06:256 KiB and
1 MiB exact binary transfers, half-close/EOF, clean stop and zero TX failures.
See VERIFICATION.md and tcp-stream-20261106-112110-19055. Remaining roadmap
work includes broader fault closure, experiments, operational tooling and release.


Latest integrated Release regression:17/17 tests passed on2026-11-06 in
integration-closure-20261106-112426-19612, including the latest compiler/control
and expanded fault checks. Offline evidence indexing and bounded manifest-verified
source packaging are available; hosted CI/fresh installation remain unverified.


Source archive fresh-directory reproduction passed18/18 CTest tests on2026-11-06
(source-reproduction-20261106-113624-21020). A subsequent bounded reference
per-rule ACL diagnostic change passed native linear/indexed ASan/UBSan; Linux
regression is pending. No claim of all-rule or worker/XDP per-rule export.


The per-rule ACL diagnostics passed fresh source reproduction and18/18 Linux
CTest tests in source-reproduction-20261106-113932-21544. Live packet-driven
rule snapshot validation is the next targeted gate; no unrelated rerun needed.


Fleet endpoint outage/rejoin acceptance passed on2026-11-06 in
fleet-outage-20261106-114929-23649: unavailable-member preflight, partial-state
retention, no blind replay, explicit generation11 corrective convergence and
clean shutdown. REMAINING.md is the current open-work ledger; historical roadmap
bullets should not be treated as all still pending.
