# Remaining implementation and acceptance ledger — 2026-11-06

This is the current open-work ledger. ROADMAP.md retains original workstream
scope, including already-completed bullets. A full-original-roadmap completion
claim requires resolving these items or an explicitly agreed scope change.

## Completed gates: do not repeat without a relevant change

- Reference/worker state, NAT/services, selection policies and dynamic reference
  neighbors; TSan and live worker reload/API acceptance.
- XDP real verifier/live bytes, owner-aware lifecycle, kernel counters, HTTP/audit
  and portable policy interchange/native conversion.
- DPDK real EAL/ring PMD, partial-TX ownership, bounded PCAP/shutdown/parity.
- Durable reference checkpoints/exact-child recovery; local fleet partial rollout,
  lost-response reconciliation and endpoint outage/rejoin via corrective plan.
- Reference persistent TCP, half-close, bounded exhaustion/malformed ICMP tests.
- First16 reference ACL diagnostics, bounded audit and offline snapshot view.
- Manifest-verified archive and fresh-directory Linux build18/18 tests.
- Historical indexed lookup experiment; capped worker comparison with no speedup.
- Health flapping/all-backend outage/recovery with real acknowledged publications
  and per-endpoint health metrics; deterministic owned-lab UDP loss/recovery.
- Verified Linux single-entry offline demo and newly labeled internal lab
  provisioning/live UDP acceptance from the prepared local image.

## Remaining functional work

1. DPDK continuous live-I/O service beyond finite captures remains open. The
   generalized capture path and authenticated bounded capture-job control now
   passed actual hosted DPDK acceptance; they do not imply live NIC semantics.
2. Durable worker configuration restart is implemented with mode/count binding;
   real supervisor-crash/pidfd/traffic acceptance is pending hosted CI. Individual
   owner replacement, dynamic discovery/global least-connections remain unsupported.
3. Broader fault matrix: controlled reordering and additional TCP/ICMP failure
   boundaries/recovery; deterministic complete-loss UDP interval has now passed.
4. Per-backend traffic observability and pre-receive socket-drop provenance.
   Per-endpoint health observation metrics now passed live acceptance. Reference
   per-rule exports are capped16; worker/XDP per-rule exports are not implemented.
5. General deployment/control scope: secure remote channel/node membership and
   independent failure domains remain unimplemented; management is loopback-only.
   Durable worker/XDP restart and stateful XDP exception fallback are not provided.

## Remaining experiment and release work

6. Equivalent supported-policy network load/loss/latency comparisons with retained
   raw observations and profiling. Capped offline invocation rates do not close it.
7. Fresh-image/machine bootstrap and consolidation of historical acceptance scripts
   into a maintained entry point. Existing-image provisioning and live traffic,
   plus the offline operator demo, have now passed.

8. Final CI after remaining functional changes, capability/docs/version
   synchronization and reviewed release packaging. Hosted CI itself is now
   verified: run42178801073 passed on the latest DPDK binding commit
   e6a9ae9a7d019b117caf7f6febb45477cce0a8b0. DraftPR1 remains unmerged.

Physical NIC/NUMA/scaling studies need other isolated Linux hardware. A paper or
novelty claim is conditional; code cannot certify interview understanding.

## Execution constraints

Keep manual Docker output gates,2-CPU/~2-GiB VM,512-MiB builder,128-MiB lab caps,
serial builds,10-GiB Mac disk guard. Latest local DPDK attempt observed8.89 GiB and stopped before execution.
The10-GiB guard currently blocks local build expansion.
Approved deletion of11 obsolete builder directories completed; no further deletion
is authorized. Avoid build/install/container expansion at this narrow margin. No-build focused
checks may resume after their own guard check; source-only work can continue. Reuse
verified artifacts where source equality permits. Ask before deletion.
