# Changelog

## 0.1.0-rc1

Added a C++20 reference packet appliance, validated configuration grammar, IPv4
LPM/ACL, bounded UDP state/SNAT/services, ARP/ICMP handling, offline CLI, metrics,
benchmark tooling, Linux AF_PACKET adapter source and test/build/CI documentation.
Native sanitizer verification is available; live Linux acceptance remains pending.

## Unreleased extension — 2026-10-18

- Added opt-in TCP handshake tracking, NAT/PAT and service round robin; Linux
  sanitizer and live real-socket smoke checks passed for this TCP batch.
- Added return-path related ICMP NAT error translation, including quoted checksums.
- Added explicit backend draining with old-flow affinity and fail-closed new flows.
- Added authenticated loopback supervisor, acknowledged config publication and
  bounded sequential fleet delivery. These newer features await Linux integration.
- Added TCP helper to both builds and a sixth control-plane regression suite.

## Unreleased service-policy extension — 2026-10-18

- Added weighted round robin (bounded integer weights), least tracked connections
  and deterministic rendezvous consistent hashing for UDP/TCP services.
- Added deterministic weight-cycle, load/tie, reorder/removal stability and drain
  tests plus integrated weighted NAT/checksum regression coverage.
- Extended health proposals to preserve scheduler tokens, weights and per-service
  manual-drain ownership. Eight native suites pass; Linux policy run pending.
- Reconciled original roadmap into explicit implementation/integration/measurement
  and release gates. Recorded live control, drain, health and ICMP evidence.

## Unreleased dynamic-neighbor extension — 2026-10-20

- Added opt-in solicited ARP cache and bounded per-target/global pending queues.
- Added bounded idle maintenance, retry/negative-cache expiry, ARP request rate
  budget and policy revalidation before queued packet emission.
- Integrated timer outputs into AF_PACKET; static neighbor behavior stays default.
- Separated deferred/consumed decisions and exposed queue/reprocessing metrics.
- Eight native suites pass; updated Linux/live neighbor acceptance pending.

## Unreleased acknowledged health reconciliation — 2026-10-20

- Added authenticated acknowledged-config reads and a finite health controller.
- Health decisions publish only when changed; accepted state is tied to the active
  revision/hash, with stale/manual-drift detection and failed/uncertain round isolation.
- Added bounded round counts, jittered cadence, explicit target scope and a ninth
  regression suite. Live integration remains pending.
- Recorded successful live dynamic ARP UDP/TCP forwarding and cache reuse.

## Unreleased observability extension — 2026-10-20

- Added engine bytes, ACL/route work, flow expiry/high-water and queue peak metrics.
- Added per-port socket RX/TX bytes/packets/failures and process resource snapshots.
- Expire flows during idle maintenance; tests guard accounting and double expiry.
- Bound port identifiers and escape metric labels without per-flow cardinality.
- Preserve complete-snapshot terminal marker; Linux/live validation pending.

## Unreleased mixed-flow baseline tooling — 2026-10-20

- Added bounded bench-mix warm/cold processing with measured mapping counts and bytes.
- Added repeated multi-size/multi-flow UDP trials, raw data, artifact hashes,
  descriptive summaries and per-trial/global runtime budgets.
- Added CLI mode/state/bounds regressions and verified 24 sanitizer harness trials.
- Recorded successful live observability accounting and clarified inherited peak RSS.
- Nine native suites pass; Linux validation/optimized baseline remain pending.

## Unreleased optional flow indexing — 2026-10-20

- Added complete-tuple/interface/protocol forward/reverse indexes and NAT occupancy.
- Added deadline-gated expiry and safe index rebuild after partial reclamation.
- Kept linear mode as default/reference; added dual core regression/replay checks.
- Added counterbalanced same-binary A/B harness and combined bounded Linux/live batch.
- Preserved original limits, policy snapshots, port reuse and TCP timeout transitions.

## Unreleased XDP kernel-check batch — 2026-10-20

- Added separate eBPF stateless IPv4 routing/ACL subset and portable native model.
- Added reference byte parity, malformed/bounds/ACL/LPM tests and libbpf kernel checker.
- Added size-guarded compiler install and a no-network capped checker container
  with BPF/NET_ADMIN and a default-deny seccomp profile, preserving existing caps.
- No bpftool dependency, interface attachment, unconfined mode or automatic deletion.
- Eleven native summaries pass; kernel compilation/verifier/test-run pending.
