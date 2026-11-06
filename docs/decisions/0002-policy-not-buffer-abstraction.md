# ADR 0002: Share policy semantics, not packet-buffer mechanics

**Status:** Accepted — 2026-09-14

Linux packet sockets, XDP, and DPDK expose materially different packet lifetime, metadata, allocation, and queue semantics. A universal `PacketBuffer` abstraction this early would hide exactly the systems differences NetForge is meant to teach.

Shared artifacts will instead be packet vectors, policy schemas, action results, and metrics definitions. Each datapath owns its packet representation and explicit conversion boundary.

