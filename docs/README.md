# Documentation

All commands assume the repository root unless stated otherwise. The supported
release is0.1.0; the guides below describe current behavior.

## Pick a path

| Goal | Read in order |
|---|---|
| Understand the project in two minutes | Root README → [architecture](architecture.md) → [capabilities](capabilities.md) |
| Run and demonstrate it | [Getting started](getting-started.md) → [demo walkthrough](demo.md) |
| Evaluate the evidence | [Results](results.md) → [committed raw observations](../results/portable-0.1.0/README.md) |
| Operate an isolated Linux lab | [Live lab](live-lab.md) → [operations](operations.md) → [security](../SECURITY.md) |
| Explain it in an interview | [Portfolio guide](portfolio.md) → [design reference](reference/design.md) |

## Implementation reference

- [Configuration](reference/configuration.md), [packet semantics](reference/networking.md),
  [design decisions](reference/design.md), [worker contracts](reference/workers.md).
- [Observability](reference/observability.md), [durability](reference/durability.md),
  [fleet reconciliation](reference/fleet.md), [backend contracts](reference/backend-contracts.md).
- [Control API](../control/README.md), [mTLS nodes](../control/SECURE-NODES.md),
  [capture-job service](../control/CAPTURE-SERVICE.md).
- [XDP build/lifecycle](../backends/xdp/README.md), [portable policy format](../backends/xdp/WIRE-FORMAT.md),
  [DPDK build/ownership](../backends/dpdk/README.md).

[Historical material](archive/README.md) preserves the learning milestones and
append-only development logs. Statements such as “pending” there describe the
past, not the current release. Use the current guides and results for evaluation.
