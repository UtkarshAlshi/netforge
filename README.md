# NetForge

NetForge is a correctness-first programmable network appliance and systems-learning program.  Its common policy model will be implemented and evaluated over Linux packet sockets, XDP/eBPF, and (later) DPDK.

## Current milestone: M0–M1

This repository currently contains a reproducible Linux network-namespace lab and a deliberately small, bounds-checked Ethernet/IPv4 decoding laboratory.  It is not yet a router; building one before the packet model, tests, and lab are trustworthy would be premature.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

For sanitizer coverage on Clang or GCC:

```bash
cmake -S . -B build-sanitize -DNETFORGE_ENABLE_SANITIZERS=ON
cmake --build build-sanitize && ctest --test-dir build-sanitize --output-on-failure
```

## Repository guide

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — evolving system boundaries and invariants
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — gated milestones and exit criteria
- [`docs/decisions/`](docs/decisions/) — architecture decision records
- [`docs/learning-log.md`](docs/learning-log.md) — concepts demonstrated and open questions
- [`lab/`](lab/) — isolated namespace topology and capture instructions
- [`src/`](src/) — packet-model code, later datapaths and control plane
- [`tests/`](tests/) — unit, packet-vector, integration, and fault tests

## Non-negotiable rules

1. Packet data is untrusted; parser reads require a preceding bounds check.
2. Correctness and packet-level evidence precede optimization claims.
3. Every feature is introduced with a design note, tests, and an observable experiment.
4. Measurements are retained with environment metadata; no fabricated performance claims.
