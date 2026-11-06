# XDP stateless subset — initial kernel acceptance verified

This is a separate eBPF datapath, not the userspace engine renamed. program.bpf.c
uses a one-element BPF policy array, bounded 32-rule/16-route classification,
IPv4 header checksum validation, longest-prefix selection, static Ethernet rewrite
and TTL/checksum repair. Routing returns XDP_REDIRECT (or XDP_TX for same-interface
output). Bounds/malformed-header/default-deny decisions return XDP_DROP.

The common policy.h is compiled as C for BPF and exercised natively against the
C++ reference for a valid UDP routing fixture. Full native tests pass. BPF compilation, kernel verification and six kernel test-run fixtures passed
on Docker Linux 6.10.14-linuxkit aarch64 on 2026-10-20. The expanded 312-case
kernel corpus also passed on 2026-10-20. Native success alone does not prove verifier safety.

## Deliberate support boundary

- Stateless IPv4 routing and ordered ACLs on complete unfragmented IHL-5 packets.
- Protocol/port classification for TCP/UDP; basic ICMP length classification.
- IPv4 header checksum checked; transport checksums and TCP state are not checked.
- No NAT, load balancing, dynamic ARP, ICMP generation, fragmentation/reassembly,
  multicore state or performance claims. A restricted shared-parser compiler is described below.
- Non-IPv4, IPv4 options/fragments, unknown protocols, TTL expiry, no route, MTU
  exceptions and frames for a different ingress MAC return PASS to the next stack.
  PASS is an exception/punt, not successful routing or full firewall acceptance.
  The current managed runtime requires kernel forwarding disabled; no automatic
  userspace reinjection is provided. PASS enters the container kernel stack and
  must not be treated as stateful policy enforcement. Use the complete reference
  backend when NAT/stateful enforcement is required.
- Kernel corpus policy comes from fixture.h; the live checker also accepts the restricted compiler output described below. The map
  is initialized once and user-frozen per program instance. The managed engine
  now publishes prepared instances with conditional owner replacement through
  the authenticated API, as verified below. Two mutable array banks
  are not presented as atomic/RCU publication.

## Isolated verification batch

User runs `../../../netforge-live/xdp-batch.sh` (actual shared outputs layout). It:

1. Requires at least 12 GiB free on the Mac, then copies source to a fresh builder
   directory and runs the full Linux sanitizer suite, including native XDP parity.
2. Simulates installation of clang-18/libbpf-dev/pkg-config without bpftool; refuses
   missing size metadata, more than 100 selected packages, gross installed size
   above 768 MiB or downloads above 256 MiB. No apt update/upgrade is requested.
3. Installs only that plan inside the existing 512-MiB/.5-CPU build container,
   compiles a real BPF ELF plus native libbpf checker and bundles at most 32 MiB.
4. Creates a separate named netforge-xdp-check-* container with network none,
   memory/no-swap 128 MiB, .5 CPU, 64 PIDs, drop ALL/add BPF, NET_ADMIN and PERFMON,
   no-new-privileges and the explicit default-deny seccomp profile in this folder.
   SYS_ADMIN, privileged mode, host networking/mounts and unconfined seccomp are
   not used. The profile permits runtime calls/bpf, rejects namespace-creating
   clone flags and returns ENOSYS for clone3 fallback.
5. Loads this program/map into the Docker Linux kernel and runs six bounded
   synthetic test-run cases. It never attaches to an interface or enables the
   BPF_F_TEST_XDP_LIVE_FRAMES flag. See the kernel's
   [test-run semantics](https://docs.kernel.org/bpf/bpf_prog_run.html).
6. Closes object descriptors, stops the newly-created container and retains it for
   inspection. No container/image/volume/data deletion is attempted. Existing
   NetForge containers/caps/routes/sysctls/resource limits remain unchanged.

The checker tests route/TTL/MAC/checksum rewriting, default deny, bad IPv4 checksum,
TTL/fragment exception punts and ARP punt. If load/seccomp/capability verification
fails, the batch stops; do not substitute a privileged or unconfined container.
Initial generic interface attach/detach and veth traffic parity passed. A general policy compiler,
control-plane updates/rollback, kernel-drop observability and measured XDP network
performance remain further acceptance gates.

Build tooling is based on [libbpf's public APIs](https://github.com/libbpf/libbpf).
The BPF program is offered under MIT OR GPL-2.0-only and uses the kernel-recognized
Dual MIT/GPL license tag. Project-authored userspace files use the root MIT license.
Bundled distribution libraries retain their respective licenses; this local runtime
bundle is not an approved public release artifact.

### Checker capability requirement

The isolated checker requires BPF, NET_ADMIN and PERFMON on Linux 6.10.
The latter selects the privileged verifier rules for variable packet-pointer
arithmetic (`bpf_bypass_spec_v1` in include/linux/bpf.h); UID 0 alone does not
provide this permission after cap-drop ALL. The checker remains network-none,
128 MiB/no swap, 0.5 CPU, 64 PIDs, no-new-privileges and default-deny seccomp.
The profile does not allow perf_event_open. No SYS_ADMIN or privileged mode is
used. These capabilities apply only to the new temporary checker, not the
builder, existing appliance or macOS. Initial kernel verification and six fixture test-runs passed; expanded corpus
acceptance also passed.

### Private live veth acceptance (initial fixture verified)

Run outputs/netforge-live/xdp-live.sh using the manual Docker workflow. It builds
a separate C/libbpf acceptance executable with the existing toolchain, bundles
its runtime, and creates a fresh network-none container with the same 128 MiB,
0.5 CPU, 64 PID caps. The dedicated seccomp-live-aarch64.json additionally
permits socket calls; NET_RAW is added for raw Ethernet injection/capture.
No perf_event_open, SYS_ADMIN, host networking or bind mounts are enabled.

Only new nf-source/nf-in and nf-out/nf-sink veth pairs inside that namespace are
created. No IPv4 addresses or kernel forwarding are configured. The checker
refuses to replace an existing XDP program, attaches generic XDP to nf-in,
redirects fixture traffic to nf-out, and observes nf-sink. Three positives
compare all Ethernet/IP/UDP bytes with the native model. Interleaved ACL-denied
and bad-IP-checksum frames must not appear at egress within 200 ms. These are
bounded functional checks, not loss or throughput measurements. Cleanup uses
the owned program FD for conditional detach and verifies absence; the script
stops and retains the new container even on failure. Kernel namespace teardown
is the fallback cleanup if the process times out. Existing lab nodes are untouched.

The initial fixture-based private live test passed on 2026-10-20: all three
redirects, both negative observations and verified detach. The script now also
compiles examples/xdp-veth.conf with the shared C++ parser inside the private
namespace using actual interface indices. This config-integrated variant passed on 2026-10-20 with generation 1,
1044-byte policy and actual namespace-local bindings.

### Restricted shared configuration compiler

`nf-xdp-compile CONFIG INGRESS OUTPUT PORT=IFINDEX ...` runs the reference
parser/validator and then applies stricter backend limits. It requires all ports
explicitly bound to unique positive indices, default deny, at most 32 UDP ACLs
and 16 static routes. Gateway routes require a static neighbor; direct routes
require a /32 static neighbor. Routes including appliance-owned IPs are rejected.
NAT, services, TCP state, discovery, indexing and non-UDP ACLs are rejected,
never silently omitted. Rules retain order and port/prefix matching.

Output is a 1044-byte target-native policy ABI, intended for this same-architecture
local bundle only, not a versioned portable interchange format. Generation is
reported by the compiler but not embedded in this policy. The live checker
checks size and expected acceptance bindings, publishes once and freezes the
map. This is initial configuration integration; dynamic API reload, per-port
policy banks and transactional rollout remain unimplemented. PASS exceptions
still need a downstream policy before general deployment.

For live bindings, `PORT=@DEVICE` resolves the configured device in the current
network namespace with if_nametoindex. Mismatching or unresolved device names
are rejected. This avoids shell subprocess/output capture in the restricted
checker. Numeric bindings remain available for offline compilation.

## Prepared expected-owner replacement/rollback acceptance

The extended live_check.c prepares a second independent program/map pair with
a frozen default-deny policy. It verifies the candidate map rejects update with
EPERM, deliberately supplies the wrong expected old program FD and requires
replacement rejection with unchanged attached ID and forwarding, then replaces
using the exact owned original FD. A real veth frame must be denied. Conditional
rollback using the replacement FD restores the exact original attached ID and
byte-parity forwarding. Cleanup conditionally detaches whichever owned program
is active and verifies absence. Existing foreign programs are never replaced.

This lifecycle acceptance passed xdp-live.sh on 2026-11-06. It is a prerequisite
for API-managed policy updates, not an implemented remote policy service, durable
XDP recovery, or packet-state migration. All interfaces are test-created veths
in a capped network-none container. No physical or macOS interface is touched.
The script reuses installed builder tools, checks resource limits and10 GiB Mac
headroom, and stages current source with its manifest; no dependency installation.

## Kernel decision counters (kernel/live acceptance passed 2026-11-06)

Each loaded program has a separate five-slot per-CPU array of64-bit packet/byte
counters. Slots1/2/4 represent DROP/PASS/ROUTE policy decisions; classification
counts precede redirect. Atomic increments avoid losing nested updates. ROUTE
is not a successful-TX/delivery counter. Input bytes include the full XDP-visible
frame, including any padding. The frozen policy map remains separate from this
mutable statistics map; no policy bytes are updated in place.

The live checker sums at most4096 possible CPU slots (64 KiB temporary buffer),
requires original-program route5/320 bytes and drop2/128, replacement route0 and
drop1/64, and emits counters labelled by original/replacement instance. Counters
remain readable while the corresponding object FD is held after replacement.
They are not persisted across object destruction, are not transactionally sampled
with traffic, and must not be merged under one monotonic series across reloads.
IPv6/ARP PASS traffic can occur independently, so exact PASS totals are not asserted.

The updated xdp-live.sh runs the312-case kernel corpus before the live lifecycle
and counter checks in the same capped private test container. No installs or
capability/profile expansion; API metrics publication remains unfinished.

The combined312-case kernel and real-veth lifecycle/counter gate passed on
2026-11-06; evidence netforge-xdp-live-20261106-102757-13885. This does not
complete API integration or establish NIC performance.

## Managed XDP engine (Linux/API acceptance pending)

engine.cpp implements the same check/run, SIGHUP publication, SIGUSR1 metrics
and SIGTERM shutdown protocol used by control/agent.py. Set NETFORGE_XDP_OBJECT
to the matching BPF object, optionally NETFORGE_XDP_INGRESS (default lan), and
launch the original agent with --binary pointing at nf-xdp-engine. Configuration
text goes through the shared compile_policy.hpp validator/encoder used by the
standalone compiler. Every port binds its configured device in the current
namespace; MACs must match and kernel IPv4 forwarding must be disabled. The
original default-deny UDP/static-neighbor subset and32-rule/16-route bounds remain.

Startup refuses an existing ingress XDP program. Reload prepares and freezes a
new map/program pair, rejects interface identity/binding or stale revision changes,
then conditionally replaces the exact owned program FD. Only a successful kernel
replacement updates ownership and emits config_published. Known preparation/runtime
rejection preserves the prior program; the agent restores the prior active file.
Lost ownership or post-publication uncertainty stops the engine rather than
reporting a safe rejection. Cleanup conditionally detaches its own active FD and
verifies absence; it never blindly removes another program.

The API can restore prior configuration semantics at a higher revision, creating
a new program ID; it does not decrement the applied revision. Metrics label
packet/byte decisions by kernel program ID and generation. Counter history is
not carried across freed objects. netforge_metrics_snapshot_complete1 is the
completion marker; Agent also accepts its legacy reference-engine marker. No
fictitious userspace RX/TX counters are added to this backend. API status and
audit retain the existing authenticated, bounded contract.

This is not durable XDP crash recovery. A killed loader can leave an attached
kernel program; restart refuses unknown pre-existing ownership. DurableAgent is
not advertised for XDP, and operator reconciliation after crashes remains a gate.
PASS exceptions still have no integrated userspace NAT/LB fallback in this mode.
Portable binary policy serialization and physical NIC performance remain separate.

xdp-api-batch.sh uses cached, source-checked BPF/core artifacts, installed tools
and the cached Python runtime. One128 MiB/.5CPU/64PID network-none container gets
BPF/PERFMON/NET_ADMIN/NET_RAW for private-veth acceptance, with no host mounts or
SYS_ADMIN. seccomp-agent-aarch64.json retains default deny and namespace-clone
restrictions, explicitly adding filesystem/process/socket calls for the short-lived
Python supervisor; it is not unconfined and the original profile is unchanged.
The pending real test covers authenticated startup, second-owner refusal, accepted
deny, higher-revision restore, runtime/preflight/stale rejection, byte-parity
traffic, program-labelled kernel metrics, audit and verified owned detach.
Native shared-compiler and original control/audit-memory compatibility tests passed.


XDP HTTP acceptance passed on 2026-11-06 in
netforge-live/xdp-api-20261106-111609-18440: authenticated startup, second-owner
refusal, generation2 deny, higher-revision restore, runtime/preflight/stale
rejection, continued byte parity, program-labelled counters, bounded audit and
owned detach with verified absence. This supersedes the pending API gate above.
No durable XDP recovery or physical NIC performance is claimed.
