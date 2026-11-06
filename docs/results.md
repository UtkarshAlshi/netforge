# Results, evidence and interpretation

The portable0.1.0 release is backed by actual packet/process tests, not just
successful compilation. [Retained raw evidence](../results/portable-0.1.0/README.md)
records the exact source revision and hashes.

## What passed

| Gate | Observed result | What it establishes |
|---|---|---|
| Portable native verification | Linux and macOS sanitizer/CTest jobs passed | Parser/state/control correctness within the tested contract |
| Real worker traffic and reload | UDP/TCP NAT, affinity, old-flow continuity and new-flow denial passed | Actual private-veth packet processing across owner threads |
| Process recovery | Supervisor SIGKILL, exact-orphan pidfd stop, higher-generation cold restart and traffic passed | Configuration recovery; **not** connection survival |
| Control and health | Real HTTP ACK/rejection, TLS certificate/token/node checks, health drain/recovery and drift refusal passed | Control behavior against actual processes |
| Receive pressure/reordering |10,000-frame pressure produced receive-queue drops;18 reordered/duplicate same-flow UDP frames each direction passed | Distinct drop provenance and tuple/payload/checksum continuity |
| XDP |312 real kernel cases and private-veth parity, conditional replace/rollback/detach passed | Restricted backend lifecycle and decisions on the tested Linux kernel |
| DPDK |312 corpus cases, independent route parity, PCAP, live PMD and cleanup passed | Actual EAL/ethdev/mbuf operation; no physical NIC claim |
| Partial TX ownership |7 sent/9 unsent; all255 mbufs reclaimed | Correct sent/unsent ownership under forced pressure |

Final release acceptance: [workflow42228227572](https://github.com/UtkarshAlshi/netforge/actions/runs/42228227572),
source `0fd44533f37e619933ff1d484ad64ec74ba49b62`.
Historical XDP acceptance was on Linux6.10.14-linuxkit/aarch64; hosted DPDK
acceptance used Ubuntu24.04/x86_64 and DPDK23.11.7. A green current CI badge
revalidates the maintained workflow, not every historical hardware/kernel case.

## Virtual-network experiment

The [committed observation table](../results/portable-0.1.0/observations.md) includes
packet rate and p50/p95/p99 latency values, with [raw trials](../results/portable-0.1.0/network-trials.json).

| Ethernet frame bytes | Trials across both modes/bursts | Verified packets | Missing |
|---:|---:|---:|---:|
|64|12|768|0|
|128|12|768|0|
|512|12|768|0|
|1500|12|768|0|
|**Total**|**48**|**3,072**|**0**|

The experiment alternates backend order, uses two repetitions for each of four
sizes and three burst values, and verifies exact emitted bytes. Raw monotonic
timing is injection-to-capture on the same host. It includes Python generator,
capture, CPU throttling and scheduler effects. Reported medians describe these
small trials; two repetitions do not support a confidence-based speedup claim.
The reference engine also performs its normal transport tracking, while the
DPDK subset is stateless. Both use the same supported policy and expected output,
not identical internal algorithms. The executable/configuration hashes are retained.

**Do not present these rates as NIC Mpps, maximum throughput or production latency.**
No physical wire overhead, saturation sweep, independent traffic generator or
Linux-kernel/XDP performance comparison is measured here.

## Other experiments and negative results

Historical offline indexed-vs-linear lookup trials showed higher-flow gains and
a one-flow regression; indexing remains optional. A24-trial capped one/two-worker
offline comparison had median ratio about0.999 and did **not** establish speedup.
Those historical measurements are source-specific, separate from the committed
network experiment, and are not current hardware scaling results. Their detailed
chronology is retained in [the development archive](archive/development/performance.md).

Physical NIC/NUMA/PMU,4–8-core, independent-machine and broader research are
explicitly deferred. These limits are part of the project's engineering story.
