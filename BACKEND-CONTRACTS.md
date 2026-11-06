# Backend operational contracts

## Reference and worker runtime

The reference engine implements the complete supported stateful feature set.
Workers partition state and PAT ranges and retain only their documented subset;
unsupported configurations fail preflight. Owner-thread failures are not a
state-replication mechanism. Process termination loses active flows. Restart from
a durable reference checkpoint restores configuration at a higher revision, not
connection state. Durable worker configuration recovery is verified with runtime mode/count binding,
exact-child pidfd stopping and a cold restart. It does not replace an individual
owner or preserve active flows.

## Managed XDP

The compiler rejects state/NAT/services/discovery and other unsupported policy
features. One owner controls the ingress program. Policy maps are initialized
and frozen before publication; replacement checks the expected old program FD.
Unknown ownership is refused. A successful clean shutdown conditionally detaches
its own program and verifies absence; SIGKILL/crash may leave a program attached.
Do not detach an unknown program or report automatic crash recovery.

PASS is a punt into the container's kernel stack, not a reference-engine call.
Kernel IPv4 forwarding must be disabled. No complete stateful firewall semantics
or automatic exception fallback is promised. Deploy the reference backend when
those semantics are required. Loaders use target-native policy ABI objects. The compiler now provides an
explicit versioned big-endian interchange format and conversion step; interface
indices remain namespace-local. See backends/xdp/WIRE-FORMAT.md. Native sanitizer and focused Linux interchange acceptance passed on2026-11-06.

## DPDK virtual/PCAP runtime

The current executable owns real EAL, ring PMDs and mbufs. Bounded PCAP input
ends at EOF, with separate routed and original-punted capture outputs. Partial
TX buffers are freed, shutdown occurs between batches, and incomplete output is
explicit. Existing capture outputs are never overwritten. Punts have no automatic
forwarding destination. This is a virtual/PCAP service, not a physical-NIC daemon
or authenticated hot-reload backend. A separate duration-bounded continuous AF_PACKET PMD service now forwards real
private-veth packets with explicit interfaces and verified cleanup. Authenticated
finite capture jobs are separately available. The live PMD service has no HTTP
hot reload or stateful fallback; physical NIC/hugepage operation is not provided.

## Measurement and deployment boundaries

Kernel XDP actions are decisions, socket sends are successful system calls,
and DPDK output captures are virtual egress observations. None alone proves
application delivery or physical NIC throughput. Separate local agents share one
Docker host and are not independent failure domains. The original agents remain loopback bearer-authenticated. An explicit secure_node
wrapper adds TLS1.3 mutual certificates, bearer authorization and node identity;
secure_fleet provides static membership and identity-bound journaled rollout.
Remote binding is opt-in and requires provisioned certificates; no automatic
firewall, host network or port-publication changes occur. See control/SECURE-NODES.md.
