# Security and threat model

NetForge is an isolated-lab project. It is not a production firewall, hardened
multi-tenant service or authorization gateway for arbitrary untrusted operators.

## Trust boundaries

- Packet bytes are untrusted: parsers check bounds, lengths and supported checksum
  fields before accessing deeper headers. Malformed inputs and resource exhaustion
  have explicit outcomes. IPv4/transport checksums are not authentication.
- Configuration authors are trusted to choose policy, interfaces and endpoints.
  Files have strict syntax/size/reference validation. An authenticated publisher
  has administrative configuration authority; there is no per-user role system.
- Original HTTP agents bind to loopback and require bearer tokens. The separate
  secure-node wrapper requires TLS1.3 mutual certificates plus a bearer token.
  Static fleet manifests bind host verification, node ID and certificate identity.
  Certificate issuance, trust-store selection and rotation remain operator duties.
- Dedicated state directories and token/private-key files require private owned
  permissions. Audit output excludes tokens and configuration bodies. Do not
  commit real credentials, private captures or deployment state.

## Packet/state limitations

Reference UDP/TCP NAT and related ICMP are implemented, but tracking is not a full
TCP sequence-window firewall or SYN-flood protection. UDP tuples can be spoofed.
Connection tables, queues, generations and neighbor discovery are bounded;
exhaustion may deny legitimate new flows. Keep CPU, memory and PID caps in labs.
Established flows remain pinned across reload, so an ACL update does not instantly
revoke them. A cold process restart loses active connections.

Restricted XDP/DPDK paths check IPv4 bounds/checksum and a limited policy. They do
not enforce reference NAT/TCP/transport-checksum semantics. XDP PASS reaches the
lab kernel and has no automatic stateful fallback. DPDK exceptions have the
mode-specific punt/discard behavior. Read [capabilities](docs/capabilities.md)
before choosing a backend.

## Privileges and safe operation

Portable replay/demo needs no network privileges. Linux live reference/worker
I/O requires NET_RAW; isolated lab construction additionally requires NET_ADMIN.
XDP requires its documented BPF/PERFMON/network capabilities and kernel support.
Use only dedicated lab interfaces, keep kernel forwarding disabled where required,
and never attach to a production/workstation NIC merely to try the project.

No `--privileged`, host networking, Docker-socket mount, broad host filesystem
mount, PCI device rebinding, hugepage configuration or macOS network change is
needed for the portable demo. Do not weaken isolation to bypass a failed test.
Durable recovery checks exact process identity and does not signal a reused PID.
An XDP crash can leave an attached program; never detach an unknown owner's program.

## Reporting a problem

Provide the source revision, minimal synthetic input, expected/observed behavior
and relevant sanitizer/log output. Omit tokens, private configuration and captures.
Use the repository's Security advisory reporting option if available for a
sensitive issue; otherwise contact the maintainer before posting exploit details.
No dedicated security response SLA is promised.
