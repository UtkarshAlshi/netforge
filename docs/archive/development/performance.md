# Performance scope

This is a reference engine, not an optimized high-performance dataplane. It copies
frames/metadata, allocates decision and pseudoheader buffers, scans routes and UDP
mappings, and performs full checksum recomputation. These choices favor auditability.

Do not advertise line rate, Mpps, reduced p99, multi-core scaling or production
availability based on the offline benchmark. No physical-NIC measurements have
been made. Sanitizer builds are correctness builds. The benchmark's samples are
local measurements only; use a separately labeled optimized build for comparisons.

Future profiling should isolate parsing, checksum, LPM, new-flow allocation,
established-flow lookup and expiry. Replace a data structure only with a measured
reason and retain a simple differential oracle. CPU pinning, SIMD, RSS and NUMA remain future experiments. XDP kernel/live
and real DPDK virtual/PCAP correctness have separate verified gates; equivalent
network performance comparisons remain unfinished. There is no claim that kernel bypass wins
for the intended workload.


## Bounded worker comparison

The worker-comparison-batch.sh experiment compares one/two workers under the
existing half-CPU builder cap. One producer submits batch16 with16 warm UDP NAT
flows and64-byte frames. Timings include frame copies, queue/future overhead and
byte validation; pool setup and warmup are excluded. Twelve alternating-order
pairs retain individual durations, rates, paired median ratio and a descriptive
bootstrap interval. Cgroup throttling counters are retained when available.
This cannot establish dedicated-core scaling or replace network delivery/latency
experiments. Both gains and regressions must be reported. No settings are raised.


## Linux worker dispatch comparison — 2026-11-06

All24 verified counterbalanced trials passed in
netforge-live/worker-comparison-20261106-113348-20538. Paired median two/one
rate ratio0.99930678475; descriptive95% bootstrap interval[0.36717,2.51077].
This wide interval provides no evidence of a worker speedup. During the run,
10 additional CFS periods were throttled and throttled time rose404815 usec.
The half-CPU cap is retained. This measures bounded offline dispatch with byte
verification, not dedicated-core scaling, network throughput or delivery.

## Maintained virtual-network comparison

`tools/network_compare.py` runs48 counterbalanced reference/DPDK AF_PACKET PMD
trials:64/128/512/1500-byte Ethernet frames, bursts1/4/16, two repetitions and64
verified packets per trial. It requires the explicit hosted-container opt-in and
already-created dedicated nf-source/nf-in/nf-out/nf-sink veths; it creates no
interfaces or qdiscs. Both modes use the same restricted configuration, original
UDP tuple and exact reference TTL/MAC/checksum bytes. The reference appliance
also incurs its normal transport tracking, whereas the DPDK subset is stateless;
this is a supported-workload comparison, not identical internal algorithms.

Every injected packet has a unique payload token. Raw injection-to-capture
monotonic timestamps, counts, trial order, frame size, burst, source configuration
hash and executable hashes are retained. p50/p95/p99 use nearest-rank order
statistics. Rates include generator, scheduler, receive/capture and idle-polling
costs under the shared .5-CPU/512-MiB container cap. Ethernet preamble/FCS/IFG are
excluded. Loss is missing injected tokens at this capture boundary, not a NIC
hardware counter. No rate sweep, saturation estimate, hardware latency, confidence
interval or speedup claim follows from these small functional trials. Physical
NIC, Linux-kernel forwarding and XDP comparison and PMU profiling remain open.
