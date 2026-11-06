# Portable release closure and deferred research — 2026-11-06

The owner explicitly selected “Finish portable release; defer hardware research”
on2026-11-06. RELEASE.md defines0.1.0. This supersedes the earlier requirement to
close every original research-roadmap gate before recognizing a release.

## Portable implementation complete

Reference/worker stateful packet processing, NAT/services and reload; reference
and worker durable configuration recovery; authenticated audit/control/health;
journaled fleet correction; explicit mTLS nodes/static membership; restricted
managed XDP; real DPDK ring/PCAP, capture management and AF_PACKET PMD service;
bounded observability and portable packaging/demo are implemented and verified.
Hosted run42227377915 passed native Linux/macOS and capped fresh-image DPDK/live
acceptance including48 counterbalanced virtual-network trials. The final version,
certificate-size preflight and documentation sync need the frozen-head CI gate.

## Deferred research and unsupported features

- Physical NIC, hugepages/NUMA/PMU,4/8-core and independent-machine experiments.
- Equivalent Linux-kernel/XDP hardware load/latency comparisons, saturation/load
  sweeps, independent generators, confidence-based performance claims and papers.
- Consensus/automatic membership or failover, packet-state replication and
  individual owner-thread replacement. Restarts lose connections.
- Durable XDP/remote-wrapper recovery, automatic stateful XDP exception fallback,
  and DPDK live HTTP hot reload. The documented subsets fail closed/reject
  unsupported configurations; these are not advertised0.1.0 capabilities.
- Production certificate issuance/rotation and deployment outside an isolated lab.

## Publication and operating constraints

Prepare exact-head green CI, verified source archive/hash and reviewable PR1.
Do not silently merge/tag or claim a public release while review is outstanding.
Keep manual local Docker execution,2CPU/~2GiB VM,512MiB builder,128MiB lab caps,
no swap, serial builds and10GiB disk guard. Hosted verification consumes no Mac
Docker space. No deletion beyond the previously approved exact list is authorized.
