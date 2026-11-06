# NetForge engineering evidence report

## System and scope

C++20 reference Ethernet/IPv4 router, ACL, UDP/TCP stateful NAT and service load
balancer; bounded worker ownership; Python authenticated control/reload, health,
durable reference recovery and journaled local rollout. XDP and DPDK use an
explicit restricted stateless policy subset; they are not interchangeable
implementations of every reference feature.

## Verified observations

- Full Linux Release build:17/17 CTest tests passed on2026-11-06 before the
  subsequent offline metrics-view addition. Expanded linear/indexed fault checks
  also passed native ASan/UBSan.
- Persistent real TCP:256 KiB direct SNAT and1 MiB service traffic matched exact
  binary payloads; half-close/EOF completed, TX2572/failures0, resident1880064
  bytes at the final snapshot. This is not a throughput experiment.
- XDP:312 real kernel test cases, private-veth byte parity, conditional ownership
  replacement/rollback, authenticated API rejection/restore, counters and detach.
- DPDK23.11.4:ring PMD, independent reference parity, forced partial TX7/unsent9,
  all255 mbufs reclaimed; external PCAP route/drop/punt and SIGTERM cleanup.
- Workers:two owners exercised by UDP/TCP/service/related-ICMP tests, TSan,
  live AF_PACKET, reload/rejection continuity and authenticated API acceptance.
- Recovery:exact-child reference restart and two logical-agent partial rollout
  reconciliation. Restarts lose flows; no multi-host availability claim.

## Historical offline lookup experiment

The retained240-trial counterbalanced experiment compares indexed and linear
lookup on historical source, not the latest package. Warm64-byte median paired
indexed/linear invocation-rate ratios:

| Flows | Ratio |
|---|---|
| 1 | 0.905 |
| 64 | 2.043 |
| 256 | 5.084 |

Ratios above1 favor indexing. The one-flow regression is retained and indexing
remains opt-in. These timings measure core invocations in a CPU-capped Docker
VM, not NIC packets/second, latency, delivery or multicore scaling. Raw summary:
netforge-live/index-comparison-20261020-164120-51094/summary.json.

## Reproduction and remaining evidence

See RELEASE-REVIEW.md for build/test and manifest-verified source packaging;
VERIFICATION.md links exact stages and retained logs. The Docker VM remains
2 CPUs/~2 GiB; builder512 MiB/.5 CPU/128 PIDs; lab128 MiB/.5 CPU/64 PIDs.
These caps deliberately limit resource use and confound unqualified speed claims.

Equivalent supported-policy network load/loss/latency comparisons, measured
worker scaling, remote failure domains, fresh-machine reproduction and hosted CI
execution remain open. Physical NIC/NUMA experiments need suitable other hardware.
This report records engineering evidence and limitations, not full-roadmap closure
or a claim of research novelty or production readiness.


## Linux worker dispatch comparison — 2026-11-06

All24 verified counterbalanced trials passed in
netforge-live/worker-comparison-20261106-113348-20538. Paired median two/one
rate ratio0.99930678475; descriptive95% bootstrap interval[0.36717,2.51077].
This wide interval provides no evidence of a worker speedup. During the run,
10 additional CFS periods were throttled and throttled time rose404815 usec.
The half-CPU cap is retained. This measures bounded offline dispatch with byte
verification, not dedicated-core scaling, network throughput or delivery.
