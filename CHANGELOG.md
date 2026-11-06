# Changelog

## 0.1.0 — 2026-11-06

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

## Demo prerequisite recovery

The demo now falls back to the existing C++20 compiler when CMake is missing.
Preflight validates tools/output/binary before creating a destination. Adds clear
recovery instructions for an earlier empty output directory and tests the direct
build on Linux/macOS. No global tools, npm settings or OS configuration are changed.

Automatic CI now uses Linux portable checks; macOS and live-backend acceptance
remain available through explicit Run workflow. Adds same-ref concurrency to
cancel obsolete runs. No historical failures or test capability are erased.
