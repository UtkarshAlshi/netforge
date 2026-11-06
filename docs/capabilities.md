# Capabilities and support boundaries

“Supported” means implemented with the documented acceptance evidence, not
production certification. Examples are synthetic policies for isolated labs.

| Capability | Reference |1–2 workers | XDP | DPDK |
|---|---|---|---|---|
| Offline synthetic replay | Yes | Worker tests/benchmarks | Native model/compiler tests | Ring/PCAP acceptance |
| Real Linux packet path | AF_PACKET sockets | AF_PACKET with owner dispatch | Generic/SKB XDP on private veths | AF_PACKET PMD on private veths |
| IPv4 routing and ACL | Yes | Supported worker subset | Restricted32-rule/16-route policy | Restricted shared policy |
| UDP/TCP state, NAT and services | Yes | Bounded owner subset | No | No |
| Backend selection/draining | RR/weighted/least/rendezvous | Subset; global least-connections rejected | No | No |
| Dynamic ARP resolution | Bounded cache/queues | Rejected | No | No |
| Related ICMP reverse NAT | Yes | Owner-affine handling | No generation/fallback | Exceptions discarded/punted by mode |
| Versioned live reload | Yes | Transactional owner reload | Expected-owner program replacement | Capture jobs fixed; live PMD restart required |
| Authenticated API | Yes | Yes | Managed XDP adapter | Bounded finite capture-job service |
| Durable configuration restart | Yes | Yes, runtime mode/count bound | No | No |
| Process crash preserves flows | No | No | Not a stateful flow engine | Not a stateful flow engine |
| Live physical NIC performance | Not measured | Not measured | Not measured | Not measured |

The portable demo/replay and core tests work on macOS and Linux. All raw packet,
XDP and DPDK runtime modes require Linux. No Windows-native validation is claimed.

## Important exclusions

- IPv6 routing, VLAN tunneling, fragmentation/reassembly and general TCP proxying.
- A full TCP sequence-window firewall or SYN-flood defense. Tracking validates
  specified handshake/ACK/FIN/RST transitions, with explicit bounds.
- Transport checksum validation in the restricted XDP/DPDK policy paths. IPv4
  checksums and bounds are checked; these are not full stateful firewall backends.
- XDP stateful fallback, durable XDP recovery, DPDK live hot reload, replicated
  flow state, automatic cluster membership/failover or consensus.
- Physical NIC/NUMA/PMU,4–8-core, independent-machine or line-rate measurements.

These are declared release boundaries, not hidden setup prerequisites. Hardware
research was explicitly deferred for the portable0.1.0 release.
