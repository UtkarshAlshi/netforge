# NetForge0.1.0 — portable engineering release

The portable project is complete within the scope explicitly accepted on
2026-11-06. It is an implemented, integrated systems project suitable for an
engineering portfolio. It is not a production firewall certification or a claim
that the original hardware/research program has been performed.

## Included and verified

- C++20 bounds-checked Ethernet/IPv4 parsing, LPM/ACL, UDP/TCP tracking, SNAT/PAT,
  service translation, round robin/weighted/least/rendezvous selection, draining,
  reference ARP discovery and related ICMP; real persistent TCP and half-close.
- One/two state owners with bounded queues and PAT partitions, reverse affinity,
  transactional reload, idle expiry, TSan and actual private-veth UDP/TCP faults.
- Authenticated configuration ACK/rejection/uncertainty, bounded audit/metrics,
  TCP health hysteresis/drain/recovery, durable reference/worker configuration
  checkpoints, exact-child crash recovery and journaled partial fleet correction.
- Explicit mTLS management, fixed node identity and static membership with
  certificate-bound rollout plans; no automatic network exposure.
- Restricted XDP compiler/verifier/live routing/filtering, expected-owner replace,
  rollback/detach, counters, API and portable policy interchange.
- Real DPDK ring PMD and PCAP processing, partial-TX ownership/cleanup, bounded
  authenticated capture jobs and a duration-bounded AF_PACKET PMD live service.
- Socket/process/rule/backend/health diagnostics, saved metrics HTML viewer,
  manifest-verified source packaging, serial verification, owned lab provisioning
  and offline operator demo. Hosted Linux/macOS and fresh Ubuntu container gates.

The capability boundaries in BACKEND-CONTRACTS.md remain part of the release.
Reference behavior is broader than worker/XDP/DPDK subsets. XDP/DPDK policy paths
validate IPv4 checksum and packet bounds; they are not complete transport checksum
or TCP-state firewalls. Exceptions have the documented punt/discard behavior.
Configuration recovery loses flow state. The remote TLS wrapper has cold startup
rather than durable recovery. No kernel/host NIC/OS reconfiguration is automated.

## Reproduce and inspect

Install CMake3.20+, C++20, Python3 and OpenSSL CLI using your normal development
environment. The portable verification performs no installation or NIC changes:

```sh
python3 tools/verify.py --sanitizers --output /absolute/new/verification-directory
```

The directory contains serial build/test logs, report.json, verified source
archive/extraction and a byte-checked offline demo. Optional privileged Linux
backend acceptance is isolated in the hosted workflow with512MiB/no swap/.5CPU/
128PIDs and dedicated private veths. No physical NIC or host mount is used.
CI artifacts retain raw trial observations for three days; download them for
long-term retention or reproduce them with the maintained harness.

## Measurements and resume wording

The48-trial reference/DPDK virtual-network comparison retains raw timestamps,
byte checks, counts and source/binary hashes. It includes generator/capture and
scheduler overhead under a CPU cap. The historical indexed lookup experiment
shows gains at higher flow counts and a one-flow regression. The capped two-owner
experiment showed no speedup. Do not turn these into NIC Mpps or scaling claims.

Suggested factual description: “Built a C++20 programmable IPv4 dataplane with
stateful UDP/TCP NAT and load balancing, bounded worker ownership, authenticated
versioned control and crash recovery; integrated restricted XDP and real DPDK
backends with sanitizer, packet-parity and fault-injection verification.”

Physical NIC/NUMA/PMU,4/8-core, independent-machine and broader comparative
research are explicitly deferred. No seamless failover, consensus, production
certification, research novelty or user interview mastery is claimed.
