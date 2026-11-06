# Configuration reference

Whitespace-separated directives, one per line. `#` begins a comment; quoting is not
supported. Maximum input file size is 64 KiB. Unknown directives, malformed numbers,
wrong field counts and invalid references are rejected. IPv4 only.

```text
version GENERATION
 default allow|deny
limits MAX_TOTAL_FLOWS UDP_IDLE_TIMEOUT_MS
port ID DEVICE IP/PREFIX MAC [MTU]
route CANONICAL_CIDR PORT direct|NEXT_HOP_IP
neighbor PORT ON_LINK_IP MAC
acl allow|deny INGRESS|* any|icmp|tcp|udp SRC_CIDR DST_CIDR SPORT|* DPORT|*
snat EGRESS_PORT PUBLIC_IP FIRST_PORT LAST_PORT
service INGRESS VIP UDP_PORT BACKEND_IP:PORT [BACKEND_IP:PORT ...]
tcp-stateful on|off
tcp-timeouts HANDSHAKE_MS ESTABLISHED_MS CLOSING_MS
tcp-service INGRESS VIP TCP_PORT BACKEND_IP:PORT [BACKEND_IP:PORT ...]
```

The space before `default` above is insignificant. Defaults: deny, 1024 flows,
30000 ms idle timeout, MTU 1500. Generation is required and positive. Limits may
appear once. Port zero is unsupported in configured service/match/allocation fields.
Protocol `any` or ICMP rules cannot match transport ports. Rules use first-match
ordering. IPv4 routes require a canonical prefix; ACL prefixes normalize host bits.

Every egress and ingress reference must name an existing port. Port devices are
unique. Neighbor addresses and gateway next hops must be on-link for their port.
SNAT supports UDP and opt-in stateful TCP, with one configured egress pool, and
must use that port's IP. UDP `service` and `tcp-service` each support 1..64
backends (equally weighted by default). TCP services require `tcp-stateful on`. There are at most
16 ports, 4096 routes, 4096 neighbors, 4096 ACLs, and 64 services.

No implicit connected routes are added; configure routes explicitly. No dynamic
ARP learning is used. ARP is answered for configured ingress IPs and service VIPs.
The configured MAC must match the real interface in live mode. Example MACs must
be replaced before a live experiment.

Tracking admits replies only for matching protocol, tuples and interfaces. TCP
tracking defaults off; when enabled, new connections require a SYN without data.
The SYN/SYN-ACK/final-ACK handshake is checked, retransmissions are supported,
and FIN/RST move state into a bounded closing period. Defaults are 10000 ms
handshake, 120000 ms established idle, and 5000 ms closing idle; each timeout
must be 100..3600000 ms. The shared flow limit includes both protocols.

This is transit connection tracking, not a TCP endpoint or hardened TCP firewall.
It does not validate established sequence windows, authenticate resets, reassemble
streams, interpret TCP options, or support simultaneous open, SYN data or midstream
pickup. When tracking is off, TCP forwarding remains stateless without TCP NAT.
Packets rejected by policy, missing neighbors or MTU checks do not commit new state.

On reload existing UDP/TCP mappings use their pinned generation. This preserves backend
selection but delays policy revocation and backend removal for those flows. New
flows use the new generation. Configuration-based draining does not guarantee an
old backend or VIP remains reachable after external network changes.

TCP mode and timeout settings cannot change during reload. TCP and UDP may use
the same VIP/port and translated port numbers because their namespaces are separate.

Backend tokens may append `/drain` (e.g. `10.77.0.130:9000/drain`). A draining
backend receives no new connections; existing mappings keep their pinned backend.
An all-draining service rejects new connections with `service_unavailable`.
Duplicate backend IP/port pairs within a service are rejected. Draining does not
probe health or terminate existing connections.

Related ICMP destination-unreachable (type 3), time-exceeded (11) and parameter
problem (12) messages are reverse-translated only when their quoted IPv4 packet
matches an unexpired UDP/TCP mapping on its return interface. At least 28 quoted
bytes are required; IPv4 options/fragments in quotes are unsupported. Quoted
addresses, transport ports, IPv4 checksum and any present transport checksum are
repaired, including truncated quotes. Path-MTU values are preserved. These errors
do not refresh state. Exact tuple matching is not ICMP authentication; spoofed
errors with a known mapped tuple remain possible. Forward-direction ICMP errors
quoting reply packets and ICMP echo NAT remain unsupported.

Checksum repair follows [RFC 1624 equation 3](https://www.rfc-editor.org/rfc/rfc1624);
ICMP NAT translation is discussed in [RFC 3022](https://www.rfc-editor.org/rfc/rfc3022).

## Service selection policies

An optional policy token precedes backend endpoints:

```text
service lab 10.77.0.100 9000 policy=weighted-round-robin 10.77.0.130:9000@3 10.77.0.130:9001@1
service lab 10.77.0.101 9000 policy=least-connections 10.77.0.130:9000 10.77.0.130:9001
tcp-service lab 10.77.0.102 9000 policy=rendezvous 10.77.0.130:9000 10.77.0.130:9001
```

The same syntax works for UDP and TCP services; TCP still requires tracking on.
`policy=round-robin` is the default. `weighted-round-robin` assigns consecutive
new-flow tickets according to integer weights 1..64 (`@N`, default 1). It is a
weighted cycle, not smooth weighted scheduling or a latency/fairness guarantee.
Non-unit weights are rejected for the other policies. Append `/drain` after the
weight when needed, for example `10.77.0.130:9000@3/drain`.

`least-connections` selects the backend with the fewest unexpired tracked mappings
for this ingress/protocol/VIP/port, including pinned generations; ties rotate.
UDP mappings approximate load and TCP handshake/closing state remains counted
until expiry. It does not measure application requests or backend CPU.

`rendezvous` uses deterministic highest-score hashing of the original flow/service
identity plus backend IP/port. Backend order does not change selection; removing
one backend remaps only keys that selected it. The hash is stable and non-cryptographic,
so it is not a defense against deliberately skewed client tuples. Backend weights
are not supported with rendezvous. Existing mappings always retain their backend
regardless of policy changes; new policies apply only to new mappings. Draining
excludes backends from every policy, and all-draining fails closed.

## Opt-in dynamic next-hop resolution

`neighbor-discovery on` enables solicited ARP discovery when a route has no static
neighbor. Default is `off`; static neighbors always take precedence. The mode is
immutable during reload. Requests use the egress interface IP/MAC. The cache is
per port/IP and accepts only replies to pending solicitations with matching
Ethernet/ARP sender MAC, local target IP/MAC, valid on-link sender and Ethernet/IPv4
ARP format. Unsolicited replies/gratuitous announcements do not populate it. This
is a stricter learning policy than the general algorithm in
[RFC 826](https://www.rfc-editor.org/rfc/rfc826.txt), not ARP authentication.
A malicious peer able to answer an outstanding query can still impersonate a host.

Fixed reference bounds: 128 positive/negative cache entries, 30-second positive
TTL, one-second negative TTL, 32 pending targets, four packets per target, and
262144 bytes of original queued frame data in total. At most three requests are
sent per target at one-second intervals, subject to eight requests globally per
one-second window and a five-second absolute pending deadline. Under target
pressure a target can expire before receiving all three attempts. Timeout drops
queued packets; no additional ICMP error is generated for resolution failure.

Queued packets reserve no new NAT mapping and do not consume a TTL decrement.
After resolution they traverse validation, current policy/routing and state lookup
again; existing flows still select their pinned generation. A queued new flow
cannot bypass a subsequently published deny rule. Related return-path ICMP errors
also use dynamic resolution when needed. Learned cache state survives compatible
reloads, but queued frames are reprocessed instead of storing stale forwarding
decisions. Cache eviction is bounded FIFO, not LRU or Linux NUD.

The Linux adapter drives `Appliance::maintenance` while idle and sends its bounded
ARP/reprocessed outputs. Offline replay/bench do not simulate ARP peers or timers;
use static-neighbor fixtures there, or the explicit maintenance API in tests.

Counters distinguish `deferred` (queued) and `consumed` (learned ARP replies) from
`dropped`. Engine received counts processing invocations including reprocessing;
`netforge_reprocessed_total` identifies the latter. Thus received equals emitted
plus dropped plus deferred plus consumed; it is not a count of unique wire packets.
ARP solicitations are maintenance outputs counted by `netforge_arp_requests_total`
and live TX counters, not by received/engine-emitted dispositions. Queue timeouts
are counted separately in `netforge_neighbor_timeouts_total`. Metric gauges expose
cache entries, pending targets and queued frame bytes. All timers use monotonic
process time; native tests supply deterministic times.

## Experimental indexed flow path

`flow-index on` enables forward/reverse hash indexes, indexed NAT-port occupancy
and deadline-gated expiry scans. Default is `off`, retaining the linear reference
path. Mode changes require process restart. The map keys include ingress/egress,
protocol and complete tuples; an ambiguous cross-direction match retains the
reference's earliest-flow/forward-first ordering. Erasing expired flows rebuilds
indexes so vector compaction cannot leave stale slots. NAT occupancy is rebuilt
with surviving mappings before expired ports can be reused.

Indexed expiry skips full scans until an earliest deadline may be due; refreshes
can cause a conservative early scan, not late expiry. TCP phase changes that
shorten timeouts update that deadline. This is not a timer wheel or multicore
implementation. ICMP association and least-connections selection still scan state;
NAT allocation still searches the configured candidate range, and routing/ACL
remain linear. Hash indexes consume additional bounded-by-flow-limit memory and
use non-cryptographic hashing; adversarial collision hardening remains future work.

The source exposes mode/index occupancy gauges. Both paths run the complete core
regression suite; CLI replay decisions are compared. Native checks pass; Linux
A/B/live indexed acceptance passed during the recorded development runs. Do not assume the indexed path is
faster for every workload before examining the paired measurements.
