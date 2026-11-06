# Security and threat model

Use only an isolated, dedicated Linux lab namespace. Packet input and configuration
are treated as untrusted for bounds and syntax; configuration authors are trusted
to choose policy. Live mode requires CAP_NET_RAW and can inject packets on bound
interfaces. It has no remote API or listening control socket. Configuration reload
is triggered by a local process signal and filesystem access.

Hard bounds cover file/frame sizes, ports, routes, rules, services, flows and
flow-pinned generations. Oversized receives are dropped; missing neighbors and
failed sends are not queued indefinitely. The live loop uses nonblocking I/O and
bounded per-interface work. Polling and scans can still consume CPU under floods;
retain container CPU/memory/PID limits.

IPv4/transport checksum validation is not authentication. UDP tuples can be spoofed.
There is no TCP stateful firewall, SYN flood defense, encrypted control channel,
NAT ICMP-error translation, dynamic ARP security, durable state or crash failover.
Fragmented traffic, IP options, VLANs, IPv6 and unsupported protocols are dropped.
ICMP query handling is limited to local echo replies. Resource exhaustion may deny
new legitimate UDP flows. Existing state survives a deny-rule reload until expiry;
this must be considered when revoking access.

Do not grant --privileged, host networking, host Docker socket mounts, broad host
filesystem mounts, or access to production interfaces for this project. Do not
change macOS firewall/routes, weaken Docker isolation, or clear unrelated data.
Report packet inputs, expected behavior and sanitizer findings in a minimal case.
