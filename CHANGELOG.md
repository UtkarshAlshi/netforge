# Changelog

##0.1.0 — 2026-11-06

Portable C++20 IPv4 dataplane with stateful UDP/TCP NAT and virtual services;
bounded workers, transactional reload, authenticated control/health/audit,
durable configuration recovery and journaled fleet reconciliation. Adds explicit
mTLS static nodes, restricted managed XDP, real DPDK ring/PCAP and AF_PACKET PMD
paths, bounded metrics, verified demos and source packaging.

Hosted Linux/macOS and capped fresh-container integration passed. The retained
network experiment checks48 trials and3,072 packets with no missing observations.
Hardware research is deferred; see [capabilities](docs/capabilities.md) and
[results](docs/results.md).

## Documentation and demo presentation refresh

Adds one-command demo launcher, browser packet walkthrough, current guides,
committed sanitized raw results and documentation checks. Moves development
chronology into the archive so it cannot be mistaken for current instructions.
The packet-engine behavior is unchanged by this presentation refresh.

[Historical implementation chronology](docs/archive/development/changelog.md).
