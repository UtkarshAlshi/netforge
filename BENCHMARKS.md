# Reproducible offline benchmark

Generate a new demo directory, then run from the repository root:

```sh
python3 benchmarks/run.py build/netforge examples/appliance.conf build/demo/nat.hex \
  --iterations 10000 --repetitions 7 --output build/offline-benchmark.json
```

The harness starts one subprocess per trial, uses the first frame repeatedly,
warms up up to 1000 iterations, and records measured iterations/elapsed nanoseconds,
emissions and drops. Reports include raw trials, median/min/max/sample standard
deviation, OS/architecture and binary/config/frame SHA-256 hashes. Output uses
exclusive creation to avoid overwriting results. It refuses a dropping workload.

This is an established-flow core microbenchmark, including output allocation and
checksums but excluding packet I/O. Time passed to the core stays fixed, so it does
not model timeout churn. No CPU frequency, background process or thermal control
is applied. Record compiler version, flags and sanitizer state separately.

For later network benchmarks: establish generator capacity independently, define
Ethernet size/FCS/preamble/IFG conventions, offered and received rates, drop counts,
latency method, warmup, repetitions, offloads, queues, affinity and topology. Use
comparable feature subsets and preserve failures/raw data. Follow applicable
[RFC 2544](https://www.rfc-editor.org/rfc/rfc2544) methodology with disclosed deviations.

## Bounded mixed-flow core trials

`netforge bench-mix CONFIG INGRESS HEX_FILE ITERATIONS warm|cold` cycles through
an in-memory corpus instead of repeating only its first frame. Warm mode primes
every corpus packet once; cold mode primes none and permits at most one pass.
The command reports measured new/tracked mappings, emitted/dropped counts, input
bytes, table occupancy and elapsed processing-loop nanoseconds. Every trial starts
a new process. Simulated packet time stays zero: this does not exercise expiry,
arrival timing, contention, receive queues or congestion. Static neighbors are
required. Bounds: 4096 corpus frames, 1 MiB decoded data, 4 MiB input text and
1..1000000 iterations. Decode/load/warmup are outside the timed loop.

`benchmarks/mixed.py` generates independent checksummed UDP fixtures and verifies
that cold trials create the expected mappings and warm trials only reuse them.
Default matrix: 1/64/256 flows, 64/128/512/1500 Ethernet bytes, cold and warm, five
repetitions. It limits each child to at most 10 seconds, total trial budget 60
seconds (configurable 5..120), iterations to 50000 and repetitions to 3..9. All
builds remain serial. Reports include each raw JSON trial, descriptive variation,
binary/config hashes and explicit build description. A failed/time-limited run
retains raw partial evidence and does not produce a successful summary.

```sh
python3 benchmarks/mixed.py /tmp/optimized-netforge --output-dir NEW_DIRECTORY \
  --iterations 5000 --repetitions 5 --flows 1,64,256 --sizes 64,128,512,1500 \
  --build-description 'record actual compiler/version and optimized flags here'
```

Cold batches with few packets are sensitive to timer noise; summary variation is
not a confidence interval. Sanitizer runs verify harness correctness, not optimized
performance. This is not a network throughput/latency benchmark and supports no
line-rate, Mpps, p99, physical-NIC, XDP or DPDK claim.

## Counterbalanced indexed/linear comparison

`benchmarks/compare.py` uses the same binary with only `flow-index on/off` changed,
identical generated workloads and fresh processes. Variant order alternates by
repetition. Default: five pairs for each 1/64/256-flow, 64/128/512/1500-byte,
warm/cold case (240 measured trials), 5000 warm iterations and 90-second total
trial budget. Each trial checks emitted/dropped counts, new/established mapping
counts and occupancy; paired workloads must agree. Raw observations, config/binary
hashes, descriptive distributions and median paired rate ratios are retained.
No speedup threshold is imposed: regressions remain reportable. CPU throttling,
background load, odd-order imbalance and tiny cold-batch timer noise remain.
Additional index memory is not measured by these timing samples.

The user-run `../netforge-live/performance-batch.sh` combines Linux sanitizer
verification of both paths, optimized paired trials and a bounded indexed live
UDP/TCP/dynamic-ARP test. It uses existing containers/limits and installs nothing.
The earlier 120-trial report is an initial baseline, not the A/B control: compare
current variants within the paired report rather than claiming improvement from
two separately collected binaries/builds.
