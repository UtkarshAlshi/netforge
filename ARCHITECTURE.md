# Architecture

## System goal

NetForge will apply a shared policy model—routing, ACL, NAT, and L4 load balancing—over different packet I/O environments.  Policy semantics and tests are shared; packet ownership and execution mechanics are deliberately datapath-specific.

```text
configuration -> validation -> immutable policy snapshot -> dataplane readers

ingress -> parse -> classify -> policy actions -> rewrite -> egress
```

## Current boundary

The M1 decoder accepts a byte span owned by its caller and produces a value-only `PacketMetadata` result. It neither retains the input pointer nor mutates bytes. This gives later pipelines a safe parsing boundary and makes parser tests deterministic.

## Core invariants

- Each layer is parsed only after the enclosing bytes are proved present.
- Multi-byte wire fields are decoded in network byte order.
- Unsupported protocol and malformed-input outcomes are explicit.
- Future packet buffers will document owner, mutability, headroom, and lifetime.
- Dataplane readers must see either an old complete configuration or a new complete configuration, never an intermediate state.

## Deliberate deferrals

IPv6, fragmentation/reassembly, TCP termination, routing protocols, a distributed control plane, and DPDK are not early dependencies. IPv4 forwarding, policy correctness, and reproducible experiments are.
