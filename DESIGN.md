# Design decisions

## D001: correct reference engine before alternate datapaths

Use C++20 spans and copied metadata. Bounds are checked before indexing. Do not cast
arbitrary packet bytes into protocol structs. Ethernet/IP input lifetime remains
caller-owned; decisions own output buffers. This costs copying and allocation but
makes ownership explicit and supports sanitizers and deterministic tests.

## D002: single worker and bounded state

Use linear LPM and linear flow lookup for a transparent reference implementation.
Default flow capacity is 1024 (maximum 65536), not an unbounded hash table. Table
scan and expiry are O(F); route lookup is O(R). NAT allocation builds a bounded
used-port set before searching the configured range. This is not a high-throughput
architecture. Optimize only after obtaining representative profiles.

## D003: explicit support boundaries

Ethernet II, IPv4 without options/fragments, ICMP, UDP and stateless TCP forwarding
are the supported processing subset. The lower-level parser can expose options
and fragment metadata; the appliance rejects those packets. No reassembly or
fragmentation is attempted. Static neighbors avoid hiding unresolved-neighbor
queues, retransmissions and cache poisoning inside this first release.

## D004: UDP state and translation

Only UDP creates tracked mappings. New-flow ACLs see original ingress addresses
and ports. Service DNAT precedes route lookup; SNAT follows egress selection.
Reverse lookup requires the expected ingress and translated inverse tuple. An
unmatched reply must pass normal ACL policy. Default-deny examples reject it.
Round robin advances only after committing a new service flow, never per packet.

## D005: reload semantics

Generation must strictly increase. Interfaces, MTUs and state limits require a
restart; routes, rules, NAT and backend sets can change. Removed backends receive
no new flows while existing mappings retain them (configuration-based draining).
There are no active backend health probes or transparent connection migration.
A failed parse/validation leaves the current generation intact. Restart loses all
flow state and may change load-balancer choices; nothing is durably replicated.

## D006: expose measurements without inventing scale

The benchmark measures repeated offline core decisions. It does not measure NIC
throughput, CPU cycles, network latency, RSS scaling or native XDP/DPDK. Keep raw
trials and context, and do not turn sanitizer results into resume Mpps claims.
