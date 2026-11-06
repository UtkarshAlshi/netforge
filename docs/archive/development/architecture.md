# Architecture

```text
CLI check/replay                  Linux AF_PACKET sockets
      |                                  |
      +----------- Appliance ------------+
                     |
             Ethernet validation
                     |
           ARP ownership / IPv4 parsing
                     |
       bounds + checksum + fragment policy
                     |
       UDP mapping lookup / ingress ACL
                     |
       local delivery / TTL exception
                     |
         UDP service destination rewrite
                     |
           LPM route + MTU + neighbor
                     |
       UDP source translation / state commit
                     |
       Ethernet rewrite + TTL + checksums
                     |
          Decision {egress, frame, reason}
```

The implementation checks TTL after local delivery; TTL 1 to the appliance itself
is not a transit expiry. The exact code order also performs service lookup before
that transit check, without committing a mapping on a failed packet.

Config is an immutable shared snapshot. The control path parses and validates a
complete new generation, checks immutable interface/state settings and the pinned
snapshot budget, then publishes one shared pointer between packet iterations.
Single-thread execution supplies the publication boundary; no claim of concurrent
RCU or atomic multi-worker publication is made.

UDP mappings preserve original and translated tuples plus ingress/egress identities
and the configuration generation. Forward and reverse packets find the same state;
new flows use current rules. Existing mapped UDP flows retain old policy/routes/
neighbors until idle expiry. An operator cannot instantly revoke them by reloading
an ACL. At most eight flow-pinned generations are admitted.

Received counts engine invocations. Each invocation has exactly one emitted or
dropped result. Emitted means the core produced a frame, not that the NIC sent it.
The live adapter separately counts successful sends, send failures and oversized
receives. Kernel/socket drops outside these points require external measurement.
