# Resume and interview guide

Link the repository root: **https://github.com/UtkarshAlshi/netforge**.
Describe the portable engineering scope you can explain and reproduce.

## Factual resume example

**NetForge — Programmable IPv4 Dataplane | C++20, Python, Linux, XDP/eBPF, DPDK**

- Built a stateful IPv4 dataplane with UDP/TCP NAT, virtual-service load balancing,
  bounded worker ownership and transactional configuration reloads.
- Implemented authenticated control, health-based draining, durable configuration
  recovery and journaled rollout reconciliation; integrated restricted XDP/DPDK paths.
- Validated packet parity, ownership and fault behavior with sanitizers and
  Linux/macOS CI; retained48 virtual-network trials verifying3,072 packets.

Tailor to the role and available resume space. The project can support systems,
networking, infrastructure and backend interview discussions; it does not guarantee
an SDE level or substitute for being able to explain the implementation.

## Be ready to explain

1. Why routing changes Ethernet next-hop addresses but keeps the IP destination.
2. How a five-tuple and translated port identify forward/reverse NAT state.
3. Why backend selection happens once per flow and how draining preserves old flows.
4. How bounded queues and disjoint PAT ranges protect ownership across workers.
5. Why reload publication, file persistence and engine acknowledgement are distinct.
6. What happens after a lost ACK, supervisor crash, PID reuse or partial fleet rollout.
7. Why XDP PASS is not a reference-engine fallback and why DPDK unsent mbufs remain yours.
8. Why core emissions, successful sends and observed delivery are different counters.
9. Why the capped worker experiment did not establish multicore speedup.
10. Which features are intentionally unsupported and what hardware research remains.

Use the [five-minute demo](demo.md) as a concrete walkthrough. Credit tools and
assistance truthfully when asked; do not claim independent authorship of work you
cannot explain. Do not claim production deployment, line-rate throughput,
seamless failover, consensus or a research publication.
