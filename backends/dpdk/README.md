# DPDK backend

This backend uses actual DPDK EAL, ethdev RX/TX bursts, mbufs and PMDs. Its
supported policy is restricted stateless IPv4 routing/ACL with static neighbors,
not reference UDP/TCP NAT or load balancing. The [capability matrix](../../docs/capabilities.md)
and [retained results](../../docs/results.md) define the boundaries.

## Runtime modes

| Mode | Entry point | Behavior |
|---|---|---|
| Ring acceptance | `nf-dpdk-ring CONFIG POLICY.bin` |312 corpus inputs, independent reference cases and forced partial TX |
| PCAP processing | `nf-dpdk-ring CONFIG POLICY.bin --pcap IN ROUTED PUNTED` | Bounded file input, rewritten routed capture and original punts |
| Capture control | `control/capture_agent.py` | Authenticated bounded finite jobs over fixed operator inputs |
| Live virtual ports | `nf-dpdk-live CONFIG POLICY.bin IN_IFACE OUT_IFACE SECONDS` | Continuous AF_PACKET PMD loop, duration1..300s, early SIGTERM/SIGINT |

Live interfaces must exactly match the checked config. No live policy hot reload
is implemented; stop and explicitly restart. The mode opens raw PMD sockets and
transmits selected frames. Use dedicated isolated Linux veths only.

## Ownership and limits

- One lcore, first allowed CPU;32MiB EAL, no hugepages/PCI/shared config/telemetry.
- Ring acceptance:255 mbufs,64-entry input ring,8-entry output ring (seven usable).
  Forced TX sends7/16; the caller reclaims the9 unsent buffers, with all255 returned.
- Live PMD: one queue/port,32-packet bursts,255 mbufs with2304-byte data rooms,
 256 PACKET_MMAP frames per port and1ms idle sleeps. Successful sends transfer
  ownership; unsent mbufs are freed once. Ports/PMDs/pool/EAL are cleaned up.
- PCAP: at most16MiB/65,536 complete Ethernet records,1..1514-byte frames;
  timestamp preservation, exclusive output files, valid partial output on shutdown.

Exceptions are counted and mode-specifically retained as punts or discarded.
There is no automatic reference-engine fallback. IPv4 bounds/checksum are checked;
transport checksums and TCP state are not. A successful PMD send is not proof of
remote application delivery. No physical NIC performance is claimed.

## Build and reproduce

The maintained fresh-container path is the `dpdk-runtime` hosted job. It caps
resources, installs a restricted matching runtime, extracts SDK headers, compiles
and runs the actual acceptance. See [live-lab instructions](../../docs/live-lab.md)
and [`tools/hosted_dpdk.sh`](../../tools/hosted_dpdk.sh).

Inside an already-designated capped Ubuntu24.04 Linux builder only, the script
uses these build entry points after its apt metadata/prerequisites are prepared:

```sh
NETFORGE_DPDK_AF_PACKET=1 NETFORGE_DPDK_BUILD_CONTAINER=1 python3 tools/install_dpdk_subset.py /tmp/new-netforge-sdk
NETFORGE_DPDK_CORE_ARCHIVE=/path/to/libnetforge_core.a python3 tools/build_dpdk_ring.py "$PWD" /tmp/new-netforge-sdk /tmp/nf-dpdk-ring
NETFORGE_DPDK_AF_PACKET=1 NETFORGE_DPDK_CORE_ARCHIVE=/path/to/libnetforge_core.a python3 tools/build_dpdk_ring.py "$PWD" /tmp/new-netforge-sdk /tmp/nf-dpdk-live
```

Do not run installer helpers on a normal workstation. The SDK/runtime must match;
x86 uses the packaged corei7 baseline, ARM its toolchain default. The dependency
helper refuses upgrades and excessive package/byte plans. It does not install the
full DPDK development dependency collection or bind hardware NICs.

Compile an example policy with the core CMake target:

```sh
/path/to/nf-xdp-compile examples/xdp-veth.conf lan /tmp/new-policy.bin lan=1 wan=2
```

The DPDK binding validates the complete config against the supplied policy before
EAL initialization. It requires lan/wan logical ports1/2 and wan egress. Unsupported
features or mismatched policy bytes are rejected. Actual hosted tests cover
multiple ACLs/routes, byte parity, PCAP errors, real live PMD sizes and cleanup.
The browser demo is the preferred portable presentation path and needs none of
these dependencies.
