# NetForge — programmable userspace IPv4 appliance

**Engineering release candidate: reference/worker live traffic, authenticated control, XDP lifecycle and DPDK virtual/PCAP acceptance verified. Full original roadmap remains unfinished.**

NetForge is a dependency-light C++20 packet-processing appliance. It combines
Ethernet/IPv4 parsing, static LPM routing, ordered ACLs, UDP/TCP connection tracking,
SNAT/port translation, virtual services (round robin, weighted round robin,
least-connections and rendezvous policies), backend draining, ARP replies, related ICMP
reverse NAT, versioned
configuration and reason-coded counters. The same processing core serves an offline
hex-frame replay tool and a Linux AF_PACKET adapter.

This release is a correctness-first reference dataplane. It is not yet a validated
high-performance, production-grade firewall. Restricted XDP and DPDK backends, bounded two-worker state ownership and active
TCP health checks have verified acceptance gates. They have distinct capability
boundaries; equivalent network performance and research publication remain open. The reference
control plane adds a loopback management agent and sequential fleet rollout; it
does not implement distributed consensus. See [control/README.md](control/README.md).

## Quick start (no Docker, privileges or downloads)

Requires a C++20 Clang compiler and Python 3. The shell build is serial; no build
system package is required. From this repository:

```sh
sh test.sh
./build/netforge check examples/appliance.conf
python3 tools/generate_demo.py build/demo
./build/netforge replay examples/appliance.conf lan build/demo/service.hex
```

Fixture generation uses exclusive creation: use a new output directory on another
run. The three service packets select backend .130, then .131, then .130 again
because the last packet belongs to the first flow. JSON decisions go to stdout;
Prometheus-format counters go to stderr. Example addresses and MACs are synthetic.

In the shared Codex workspace, keep build artifacts in work/ instead:

```sh
NETFORGE_BUILD_DIR="$PWD/work/netforge-build" sh outputs/netforge/test.sh
```

Default builds enable AddressSanitizer and UndefinedBehaviorSanitizer. Use
`SANITIZE=0 sh build.sh` for an optimized build in a separate build directory.
Compiler include-environment overrides are cleared only for compiler processes.
No global shell or OS setting is changed.

CMake/CTest is also supported (CMake 3.20+):

```sh
cmake -S . -B build-cmake -DNETFORGE_SANITIZERS=ON
cmake --build build-cmake --parallel 1
ctest --test-dir build-cmake --output-on-failure
```

## Commands

| Command | Purpose |
|---|---|
| `netforge check CONFIG` | Parse and validate a complete config |
| `netforge replay CONFIG INGRESS HEX_FILE` | One Ethernet frame per hex line; emit decisions and transformed frames |
| `netforge bench CONFIG INGRESS HEX_FILE N` | Warm up then measure the first frame N times; offline only |
| `netforge run CONFIG` | Linux-only AF_PACKET event loop; requires CAP_NET_RAW |

Live mode needs dedicated lab interfaces with matching MACs, no kernel ownership
of appliance IPs, and kernel forwarding disabled in that namespace. The program
checks these conditions and does not configure interfaces or change sysctls.
See OPERATIONS.md before running it. Do not run it against your Mac's interfaces.

## Verification and limits

Tests exercise exact parser boundaries, independent checksum oracles, routing,
ordered ACLs, bidirectional UDP/TCP translation, service affinity/draining, exhaustion, expiry,
configuration rollback, pinned generations, ARP, ICMP, MTU handling and malformed
frames. CLI tests exercise real subprocesses and independent Python decoding.
See STATUS.md for what was actually executed versus what remains unverified.

The existing Docker laboratory and this application are separate: successful
Linux-kernel pings do not prove the C++ appliance forwarded anything.

## Documentation

- ARCHITECTURE.md and DESIGN.md: packet/configuration lifecycles and decisions.
- CONFIGURATION.md: exact grammar and matching semantics.
- NETWORKING.md: ownership, byte order, checksums, forwarding and exceptions.
- SECURITY.md and OPERATIONS.md: limits, capabilities and failure behavior.
- PERFORMANCE.md and BENCHMARKS.md: honest measurement scope and harness.
- RESEARCH.md and ROADMAP.md: remaining research and engineering milestones.
- STATUS.md: release acceptance gates and resume wording.
- CONTRIBUTING.md: build/test/review workflow.


## Original repository foundation

The M0–M1 parser laboratory remains in src/common, tests/unit and packet_decode;
CMake also runs its legacy_packet_parser test. Original decision records and lab
notes remain under docs/ and lab/. Their historical milestone descriptions are
not current appliance status; see STATUS.md/REMAINING.md for the current ledger.
The original architecture note is retained as docs/architecture-m1.md. Historical
namespace scripts require their documented Linux environment; the scoped Docker
lab tool in LAB-SETUP.md is the current Mac-lab provisioning path.
