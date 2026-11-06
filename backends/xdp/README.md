# XDP backend

`program.bpf.c` is a separate eBPF packet path, using the shared bounded policy
model. It implements restricted IPv4 routing/ACL, TTL repair, static next-hop MAC
rewrite and counters. It does not implement stateful NAT/services or act as a
reference-engine fallback. See [capabilities](../../docs/capabilities.md).

## Behavior

- Policy map contains one frozen policy with up to32 rules and16 routes.
- Complete unfragmented IHL5 packets support protocol/port classification, IPv4
  checksum validation and longest-prefix selection.
- Valid routes return redirect/TX; malformed/default-deny decisions drop.
- Non-IPv4/options/fragments/unsupported protocols, TTL, no-route/MTU exceptions
  and different ingress-MAC traffic can PASS to the next kernel stack. PASS is
  not stateful policy acceptance. The managed runtime requires forwarding disabled.
- No transport checksum validation, TCP tracking, NAT, service scheduler, dynamic
  ARP, ICMP generation, reassembly or automatic userspace reinjection.

## Compiler and managed lifecycle

The `nf-xdp-compile` CMake target validates source configuration and explicit
interface bindings. It rejects unsupported stateful features. Example native ABI
compilation in a dedicated Linux namespace:

```sh
/path/to/nf-xdp-compile examples/xdp-veth.conf lan /tmp/new-policy.bin lan=@nf-in wan=@nf-out
```

Logical bindings such as `lan=1 wan=2` support offline model/DPDK tests but must
not be mistaken for actual kernel interface indices. The [versioned portable
format](WIRE-FORMAT.md) is distinct from the target-native loader ABI.

`engine.cpp` provides `check CONFIG` and `run CONFIG` through a compatible
management adapter. It inspects actual MAC/interface identities, refuses a
pre-existing program, initializes/freezes a candidate map, then conditionally
replaces only its expected current program. Rejection preserves the active
instance; owned shutdown conditionally detaches and verifies absence. Kernel
per-CPU decision counters remain instance-labelled across replacements.

A crash can leave a program attached. Durable XDP recovery is not implemented;
never detach an unknown program. The control API does not turn XDP into a
stateful firewall. [Backend lifecycle contract](../../docs/reference/backend-contracts.md).

## Build and kernel acceptance

Portable CMake tests run the native model and compiler; they do not load BPF.
Real kernel acceptance additionally requires Linux BPF/XDP support, matching
headers, clang with BPF target, libbpf, and BPF/PERFMON/NET_ADMIN/NET_RAW scoped to
a dedicated lab namespace. No production/workstation NIC is needed.

The low-level build steps, inside that prepared Linux lab, are:

```sh
clang-18 -target bpf -O2 -g -Wall -Werror -I "/usr/include/$(gcc -dumpmachine)" -c backends/xdp/program.bpf.c -o /tmp/netforge-program.bpf.o
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror backends/xdp/check.c $(pkg-config --cflags --libs libbpf) -o /tmp/netforge-xdp-kernel-check
/tmp/netforge-xdp-kernel-check /tmp/netforge-program.bpf.o
```

These commands load/test BPF and therefore are **not** the portable demo. Live
checker `live_check.c` additionally expects dedicated nf-source/nf-in/nf-out/
nf-sink veths and a compiled policy; it verifies actual redirects, negative
controls, replacement/rollback and owned detach. Its real kernel312-case and
private-veth acceptance is recorded in [results](../../docs/results.md).

The hosted native workflow rechecks model/compiler behavior; the regular DPDK
container does not imply a fresh kernel XDP run. Historical details are preserved
in [the archive](../../docs/archive/development/backend-xdp.md). Physical NIC/XDP
performance and independent-machine research remain deferred.
