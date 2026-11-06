# Worker ownership and runtime

Worker mode is a distinct supported subset of the reference appliance, not a
claim that every reference option scales across owners. Use `check-workers`
before opening raw sockets; unsupported policies are rejected.

## State ownership and limits

- One or two owners; each has a private Appliance and bounded FIFO queue.
- Forward tuple hashing assigns ownership. Reverse traffic uses the translated
  PAT-port partition to return to the same owner. Protocol is part of identity;
  a numeric port can be reused across TCP/UDP, but not for colliding same-protocol
  live mappings. Total flow/PAT bounds remain fixed across owners.
- Bounded queues/batches and packet sizes prevent unbounded buffering. Expiry and
  maintenance run even while idle. Shutdown drains bounded work and joins owners.
- UDP/TCP NAT, service translation and related ICMP have owner-affine coverage.
  Dynamic ARP/discovery is rejected. Global least-connections scheduling is
  unsupported; do not confuse per-owner state with a global load view.

## Runtime commands

After dedicated Linux lab interfaces and config are prepared:

```sh
/path/to/netforge check-workers /path/to/workers.conf 2
/path/to/netforge run-workers /path/to/workers.conf 2
```

The example `examples/workers-veth.conf` assumes nf-in/nf-out and explicit MACs;
it is not directly runnable on an ordinary host interface. NET_RAW is required.
No host route, interface or Docker setup is performed by these commands.

## Reload and recovery

Control signals are handled by the I/O coordinator. Reload quiesces packet work,
stages compatibility on all owners, and commits a complete generation. A failed
candidate does not partially update owners. Existing flows keep their old snapshot;
new flows use the accepted policy. Bounded retained generations limit accumulation.

`control/worker_agent.py` provides compatible HTTP supervision. The separate
`control/durable_worker_agent.py` adds durable acknowledged configuration recovery,
binding persisted runtime mode and worker count. An exact orphan is stopped only
through verified identity/pidfd and explicit recovery authorization. A cold restart
restores config at a higher revision and loses active flow state. Individual owner
replacement, replicated state and seamless failover are not implemented.

## Verification and interpretation

Functional tests compare serial/worker outputs, reverse affinity, UDP/TCP/services,
ICMP, expiry, queue bounds, reload and draining shutdown. ThreadSanitizer acceptance
was recorded during development. Actual private-veth tests verify16 UDP round
trips, four TCP service handshakes, reload/rejection continuity, deliberate reorder,
HTTP supervision and durable process recovery. See [results](../results.md).

The historical capped one/two-owner comparison did not establish speedup. A
0.5-CPU container cannot demonstrate scaling across physical cores. Four/eight-core,
NUMA and physical NIC experiments remain deferred. Earlier implementation detail
is preserved in [the archive](../archive/development/worker-details.md).
