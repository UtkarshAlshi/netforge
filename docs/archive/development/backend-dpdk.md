# DPDK ring-PMD — initial runtime verified

This is a separate adapter using real DPDK EAL, ethdev RX/TX bursts, ring PMD,
mbufs and a bounded mempool. The initial fixture variant compiled and ran successfully on DPDK 23.11.4
on 2026-10-20. The config/reference integration also passed on 2026-10-20.

## Restricted first acceptance

- One EAL worker, first allowed CPU; 32 MiB no-hugepage EAL allocation.
- No PCI enumeration, telemetry, shared config, NIC binding, mounts or hugepages.
- 255 mbufs, zero per-core pool cache, 64-entry RX and 8-entry TX rings.
- 312 fixture/truncation/mutation inputs through actual ethdev RX/TX APIs.
- Shared stateless policy model; known route/drop/punt fixtures and exact
  emitted bytes compared to the model. This model has separate C++ reference
  and actual XDP kernel evidence; this corpus is not an independent policy oracle.
- Forced 16-frame TX burst into seven usable ring slots: expected seven sends,
  nine unsent buffers reclaimed. All pool objects must be returned after each
  batch. Device stop/close, ring/pool release and EAL cleanup on completion.

Punts are counted and freed in this offline acceptance adapter. There is no
downstream kernel path, physical packet delivery, TCP/NAT state, general config
publication, continuous service, multi-worker ownership or performance claim.
This first fixture-based test does not complete the whole DPDK roadmap gate.

## Dependency footprint

The full libdpdk-dev simulation selected 232 packages, 208,203,776 bytes gross
installed and 43,922,322 bytes download, with no missing size metadata. It also
selected systemd/udev/RDMA and all PMDs. None of that full plan was installed.
The Docker VM's apparent 181-GB free filesystem is not Mac free disk; the host
guard uses actual Mac filesystem headroom.

The batch instead simulates only librte-net-ring24, librte-mempool-ring24 and
libbsd-dev dependencies. It rejects upgrades, systemd/udev/rdma-core, more than
64 runtime packages, gross dependency+SDK size above 256 MiB or downloads above
128 MiB. It installs this reduced runtime plan only inside the existing capped
builder. It downloads and extracts libdpdk-dev as an SDK archive without
installing that package or executing its package scripts. Runtime and headers
come from the same cached repository candidates; exact plan/log retained.

Mac disk must remain at least 10 GiB free before starting; no apt update/upgrade,
new container, privilege change, service setup or automatic deletion is requested.
The existing builder remains 512 MiB/no swap, 0.5 CPU, 128 PIDs.

Implementation follows DPDK's ring-PMD API and ownership model:
https://doc.dpdk.org/guides-23.11/nics/pcap_ring.html
https://github.com/DPDK/dpdk/blob/v23.11/drivers/net/ring/rte_eth_ring.c

## Recorded initial acceptance and next gate

Initial batch passed on 2026-10-20 with 19 runtime packages, 75.33 MiB gross
SDK+runtime estimate and 14.48 MiB downloads. All 312 cases, forced partial TX,
buffer reclamation and shutdown passed. Counters: received328/routed158/
dropped131/punted39/sent149/unsent9. These counters describe ring-PMD tests only.

The next revision takes CONFIG POLICY.bin. The shared compiler creates the
restricted stateless policy with logical ingress/egress bindings 1/2 (these
are policy identifiers, not Linux interface indices). The DPDK ethdev port is
separate. The acceptance program loads the same config in the real C++ Appliance
and compares nineteen common-subset route/deny decisions, including exact
forwarded bytes on valid UDP and sixteen distinct source ports during pressure.
Other corpus mutations still compare the common native model: transport-checksum
validation and TTL/fragment/ARP exception semantics deliberately differ.

Use dpdk-integration.sh to reuse the verified SDK without installing packages or
creating containers. No independent production traffic, portable policy ABI,
control API lifecycle, continuous service or physical PMD result is implied.

Configured integration passed on 2026-10-20: generation-1 shared compiler policy,
all nineteen independent C++ comparisons and the full corpus/partial-TX/cleanup
checks. Counters RX328/routed157/dropped131/punted40/sent148/unsent9. The changed
route prefix (/32 configured vs /24 fixture) explains the different corpus
counts; these are functional case counts, not network performance.

## Bounded PCAP adapter (actual DPDK acceptance passed 2026-11-06)

`nf-dpdk-ring CONFIG POLICY.bin --pcap INPUT.pcap ROUTED.pcap PUNTED.pcap`
processes external regular capture files through real ring-PMD RX/classification/
TX. The declared compiler subset remains restricted: one UDP ACL, one route,
logical ingress1/egress2. It does not implement NAT, TCP state, live NIC polling,
API publication or a downstream exception stack. Original PASS packets are
preserved in the separate punt capture instead of being called forwarded.

Input is little-endian, microsecond-resolution Ethernet PCAP v2.4, at most16 MiB/
65,536 records,1..1514 captured bytes per record, complete captures only. Timestamp
microseconds must be below1,000,000. PCAPng, truncated records, snaplen violations,
zero-length records and captured/original length mismatch are rejected. Inputs
must be regular files. Output files are exclusively created with mode0600 and
never overwrite existing paths; timestamps and frame order are retained. Outputs
are flushed/fsynced before a complete result. A failure can leave retained partial
files; success is claimed only by exit0 with complete:true and cleanup success.

Four-record batches use the existing255-mbuf pool and bounded input/output rings.
Sent ownership transfers to the output ring; dequeued mbufs have RAII ownership
during output I/O; dropped/punted/unsent frames are freed. Each completed batch
requires empty rings and all255 mbufs available. The existing forced partial-TX
fixture remains a separate test. SIGINT/TERM stops at a bounded batch boundary,
flushes valid partial captures, emits complete:false, releases port/rings/pool/EAL
and returns130. No busy polling or full-file buffering is introduced. DPDK EAL
still uses no hugepages/PCI/shared configuration and32 MiB memory budget.

Native ASan/UBSan PCAP I/O tests passed. dpdk-pcap-batch.sh passed the Linux gate after the reference repair: original312-case/partial-TX/reference acceptance;163 external records with
64 independently checked routed frames across64/128/512/1514-byte sizes,96 drops,
three exact original punts, timestamp/private-output checks, empty input, ten
malformed/budget cases, existing-output refusal and in-flight SIGTERM cleanup.
It reuses the installed SDK and verified CMake core archive after checking source
equality, avoiding core recompilation. It creates no container or dependency
installation, preserves evidence and keeps captures in the capped builder.

The first PCAP gate passed the existing DPDK corpus but exposed reference
normalization of zero UDP checksums on untranslated routing. The reference is
repaired to preserve zero when the IP/port tuple is unchanged; NAT repair is
retained. Native common-fixture parity now passes. Run checksum-reference-batch.sh
for the Linux full regression and DPDK acceptance using the rebuilt core. The
standalone dpdk-pcap-batch.sh accepts NETFORGE_DPDK_CACHED_BUILD pointing at a
verified stage with source/ and build/; it refuses a core with different source.
Do not disable the equality checks to reuse a stale reference.

Verified PCAP evidence: dpdk-pcap-20261106-105952-17272. Full17-test Release
reference gate: checksum-20261106-105900-17225. Future runs default to this
repaired reference cache and build the current policy compiler against its
source-verified core archive. Historical failed captures remain retained.


## General supported capture-policy binding

PCAP mode now accepts the shared compiler's multiple-rule/multiple-route subset
(up to32 rules/16 routes), with exactly two logical ports lan1/wan2 and all routes
emitting towan2. The fixed synthetic corpus mode retains its one-rule/one-route
fixture constraint. Before EAL initialization, the runtime recompiles configuration
and compares canonical policy fields, rejecting mismatched MACs/actions/routes
or unsupported state/NAT/discovery. Native padding and inactive slots are not
policy identity. This prevents a separately supplied blob from silently applying
different policy than the configuration used by the reference oracle.

Native ASan/UBSan binding tests passed. Actual multi-policy capture acceptance
is pending the expanded dpdk-pcap-batch.sh. This does not add a continuous NIC
service, hot reload, remote API or stateful DPDK semantics.

## Continuous virtual-port service

`live.cpp` provides a separate real AF_PACKET PMD receive/transmit loop on two
explicit Linux interfaces. It uses DPDK's PACKET_MMAP driver, not the synthetic
ring-PMD adapter. Build with `NETFORGE_DPDK_AF_PACKET=1` and the matching restricted
SDK/runtime. Invoke `nf-dpdk-live CONFIG POLICY.bin IN_IFACE OUT_IFACE SECONDS`.
The checked config must bind lan/wan to those exact interfaces; policy bindings
remain logical1/2. Duration is1..300 seconds, with early SIGTERM/SIGINT stop. No
PCI devices or hugepages are used. One lcore,32-MiB EAL,255 mbufs,32-packet bursts,
2304-byte mbuf data rooms, one queue/port and256 PACKET_MMAP frames bound allocations. Idle polling sleeps
one millisecond. This is a safe functional service, not a tuned line-rate design.
Successful TX transfers ownership to the PMD; unsent mbufs are freed once.
Counters distinguish routed, policy drops, exceptions discarded and unsent TX.
Exceptions have no stateful fallback; do not deploy this as a NAT appliance.

Use only dedicated isolated Linux lab interfaces with NET_RAW. There is no
interface creation, address/route change, published port, device rebinding, host
mount or automatic deletion in this executable. It does open raw PMD sockets and
transmit selected frames. Configuration replacement and HTTP supervision for this
mode are not implemented; stop and explicitly restart after validation. Hosted
acceptance checks private-veth byte parity, negative controls and clean shutdown.
Physical NIC, NUMA and cross-backend performance measurements remain separate.
Driver options follow https://doc.dpdk.org/guides-23.11/nics/af_packet.html .
