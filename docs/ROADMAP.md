# Roadmap and gates

| Milestone | Outcome | Exit evidence |
|---|---|---|
| M0 Lab | Namespace topology and packet observation workflow | topology script, captures, packet walk note |
| M1 Packet lab | Safe Ethernet/IPv4 decoding | unit tests, captured frame fixture, sanitizer pass |
| M2 Engine | Packet loop, metadata, actions, counters | deterministic pipeline tests |
| M3 Router | ARP, IPv4 LPM forwarding, TTL/ICMP | two-router namespace integration tests |
| M4–M6 Policy | ACL, conntrack/NAT, L4 LB | policy tests and failure cases |
| M7 Control | versioned, atomic config updates | rollback/concurrent-reader tests |
| M8 Operations | metrics, logs, controlled capture | topology-level diagnostic runbook |
| M9 XDP | constrained fast path | equivalent policy vectors and comparison |
| M10–M12 Performance | benchmark suite, DPDK, multicore study | raw artifacts, profiles, reproducible report |
| M13 Capstone | focused systems study and release | report, limitations, release checklist |

Each gate requires a design note, tests, observable packet-level evidence where applicable, and a learning-log update.

