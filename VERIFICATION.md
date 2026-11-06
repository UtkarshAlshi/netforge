# Verified execution record — 0.1.0-rc1

## Linux configuration-update acceptance

Live reload test started on generation3 with one established service flow. SIGHUP
published valid generation4, which denies new UDP. The established tuple continued
to reach backend-a; a new tuple timed out. A malformed generation5 update generated
config_rejected. The established flow again succeeded; another new tuple timed out.
SIGUSR1 snapshot and final counters both reported active generation4, mappings1,
acl_deny2, new_udp_mapping1, tracked_udp5, arp_reply2, not_for_port1,
received11, emitted8, dropped3, successful TX8 and failed TX0. Backend replied3 times.
Process remained alive until timeout and stopped cleanly. Temporary active config
was restored afterward without changing routes, containers or resource limits.

## Linux UDP NAT/service acceptance

User disabled TX checksum offload only on A/B lab interfaces after inspecting and
recording original settings. A static bounded UDP utility compiled with Linux
GCC13.3.0 and was copied to those hosts. The generation3 application test ran two
backend endpoints on B (9000/backend-a and9001/backend-b), not separate machines.

All four probes passed endpoint/backend/token validation: direct B:9000 SNAT,
VIP10.77.0.100:9000 -> backend-a, a new flow -> backend-b, and the original service
flow -> backend-a again. Backend reply counts were3 and1. The application counted
received12, emitted11, dropped1 (not_for_port), mappings3, new_udp_mapping3,
tracked_udp5, arp_reply3, TX successful11, TX failures0, oversized RX0. Process
shutdown was clean under the bounded smoke script. This verifies live UDP return
translation, per-flow round robin and affinity; it is not a load benchmark.

## Linux transit-forwarding acceptance

User verified endpoint MACs A=5a:c6:58:10:95:ef, B=1e:09:3e:82:e3:f6 and added
host-specific routes A->B and B->A through10.77.0.120. B also has an on-link host
route to10.77.0.120. Broader reference-router routes were retained. Generation2
configuration provides explicit /32 routes and static neighbors for both hosts.

During a bounded12-second C++ process run, all3 pings received replies at TTL63.
NetForge counted received9, emitted8, dropped1, forward6, arp_reply2,
not_for_port1, tx_sent8, tx_failures0, rx_oversize0. Started/stopped events and
expected timeout status124 were observed. This verifies bidirectional live
forwarding with the actual application. It does not establish packet-loss rate
under load, MAC-rewrite capture evidence or UDP NAT/service acceptance.

## First Linux live acceptance: ARP and local ICMP

User ran the static executable in netforge-live (128 MiB, 0.5 CPU, NET_RAW only,
kernel forwarding disabled) for ten seconds. Docker IP is10.77.0.124; application
IP is the distinct10.77.0.120. Host A received both ICMP echo replies with TTL64.
Application counters: received3, emitted3, dropped0, arp_reply1, echo_reply2,
tx_sent3, tx_failures0, rx_oversize0. Structured started/stopped events observed.
Timeout status124 was expected. This verifies real packet RX/TX, ARP ownership,
local ICMP and signal shutdown. It does not yet verify transit routing or NAT/LB.

## Linux verification (user-executed Docker workflow)

The separate netforge-build container runs Linux aarch64 with GCC 13.3.0 and Python
3.12.3. It is limited to 512 MiB RAM/no container swap, 0.5 CPU and 128 PIDs.
The source was copied to /tmp/netforge-rc1 and built serially with CXX=g++,
SANITIZE=1 and NETFORGE_BUILD_DIR=/tmp/netforge-build. The actual Linux AF_PACKET
branch compiled successfully. Ethernet, IPv4, appliance and CLI suites all printed
PASS; the command returned exit status 0. No live adapter process or network
forwarding acceptance test has run yet. This supersedes the earlier Linux-build
pending statements below; CMake/hosted CI remain unverified.

Executed locally on macOS arm64 using Apple Clang 21.0.0, C++20, libc++, -O1 -g,
-Wall -Wextra -Wpedantic -Werror, AddressSanitizer and UndefinedBehaviorSanitizer.
Builds were serial. Compiler include overrides were removed per process only.

```text
PASS: Ethernet truncation, MAC fields, byte order, ownership, and length field
PASS: IPv4 bounds, fields, checksum vectors, options, fragments and padding
PASS: routing, ACLs, UDP state/NAT/LB, checksums, exhaustion, reload, ARP, ICMP and malformed frames
PASS: CLI validation, bounded input, replay, independent checksum checks and benchmark interface
```

The application tests include independent checksum verification, original/reply
tuple restoration, wrong-ingress and unsolicited return denial, first-match ACL
ordering, missing-neighbor rollback, table/port exhaustion, clock clamping,
expiry, per-flow service affinity, config-generation pinning/reclamation,
MTU/DF errors, stateless TCP, ARP VIP ownership, ICMP quoting/suppression/rate limits,
and a deterministic 2000-input malformed-frame sweep.

Executed synthetic fixture replay through the built CLI:

- Three service frames: two new mappings, one tracked mapping, all emitted.
- Backend destination sequence: 10.77.0.130, 10.77.0.131, 10.77.0.130.
- One TTL-1 frame: emitted ICMP Time Exceeded with quoted original bytes.
- Counters matched emitted/dropped disposition in tests.

Executed the repeated benchmark harness with 3 independent process trials and
1000 measured iterations each, following 1000 warmup iterations per trial. Raw
sample is benchmarks/sample-native-sanitized.json. It is a smoke test of the harness,
not evidence of network performance or improvement. No baseline comparison exists.

Shell build/test scripts passed `sh -n`. CMake/CTest and hosted CI have not yet run.
The native build compiles the non-Linux adapter stub; it does NOT compile or validate
the Linux #ifdef branch. Linux compilation, AF_PACKET TX/RX, SIGHUP under live
traffic, signal shutdown, real traffic captures and failure recovery are pending.

No Docker commands were executed, no containers were created/deleted/restarted,
no packages were installed, and no macOS/Docker resource settings changed during
this implementation pass. Existing resource constraints remain in effect.

## Live TCP integration — 2026-10-18

User executed `outputs/netforge-live/tcp-smoke.sh` against generation 6 on Linux
aarch64. Four real socket connections passed: direct TCP SNAT to Host B:9000,
then three service VIP connections selecting backend-a, backend-b, backend-a.
Backend endpoints share Host B; they are not separate machines. Each probe checked
the response label and request token. Backend replies were 3 and 1.

Observed counters: received 44, emitted 43, dropped 1 (`not_for_port`),
`new_tcp_mapping` 4, `tracked_tcp` 36, ARP replies 3, TX sent 43,
TX failures 0, RX oversize 0, generation 6. Final TCP mapping count was zero
following the bounded closing timeout. Application stopped cleanly at timeout.
This verifies live handshake/data translation and per-connection round robin;
it does not establish throughput, window validation, adversarial TCP robustness,
long-lived affinity/reload continuity, or complete original-roadmap delivery.

## Control-plane and ICMP/draining extension — 2026-10-18

Six native suites passed under the serial sanitizer build. New core regressions
cover related ICMP reverse NAT with full and minimum quotes, independent quoted
UDP/TCP checksum verification, zero UDP checksum preservation, MTU preservation,
malformed/unmapped/expired quotes, TCP established-flow deny-reload continuity,
and draining/all-draining service behavior.

Control tests use the real C++ configuration checker plus a deterministic child
runtime. They cover private file permissions, validation, stale writers, runtime
rejection and disk restoration, publication acknowledgement loss/uncertainty,
blocked subsequent publication, HTTP handler authentication, invalid plan targets,
and sequential fleet partial failure. Real HTTP socket transport, live AF_PACKET
supervision and multi-node rollout remain pending. No Docker operations were
executed by the assistant for this batch; existing lab limits remain unchanged.

## Updated Linux extension suite — 2026-10-18

User copied the current source to `/tmp/netforge-control-rc1` in the existing
`netforge-build` container and ran the serial GCC sanitizer build with a 300-second
timeout, build directory `/tmp/netforge-control-build`. All six suites passed:
Ethernet, IPv4, TCP, appliance, CLI and control-plane regression tests. This covers
the current ICMP/draining/control extension on Linux aarch64. The control suite
still uses its deterministic fake child runtime; this is not live agent/HTTP or
multi-node rollout acceptance.

## Real control-plane integration — 2026-10-18

User ran `control-smoke.py` in the existing live container with the bounded,
locally bundled Python runtime and updated static engine. All assertions passed:
incorrect bearer token rejected by actual loopback HTTP; generation 7 backend-drain
candidate acknowledged by the actual AF_PACKET process; valid-syntax immutable
TCP-timeout change rejected at runtime with previous file restored; stale writer
rejected; SIGUSR1 metrics generation 7/TX failures zero; clean owned-child shutdown.
Retained private state: `/tmp/netforge-control-smoke-1axywrdr` inside netforge-live.
This test does not prove draining under traffic or multi-node fleet delivery.

The Python runtime bundle contains 576 files/33,474,516 bytes, imported successfully
in the live container after its public-code root directory was made traversable
(mode 0755) by its owning UID 501. Agent private state remains mode 0700, tokens
0600. No container capabilities, networking or resource limits were expanded.

## Backend draining under live traffic — 2026-10-18

User executed `drain-smoke.sh`: actual control API acknowledged generations 6
and 7. A preexisting UDP flow remained on backend-a after draining; a new flow
selected backend-b and its repeat remained on backend-b. Each backend replied
twice. All test processes were bounded; retained state is
`/tmp/netforge-drain-agent-1792330684-31138` inside netforge-live. This proves
UDP draining via the real control plane, not active health detection or live
TCP established-flow draining.

## Active TCP health probe integration — 2026-10-18

User executed `health-smoke.py` in the existing live container. Real TCP probes
correctly distinguished an accepting port from a reserved non-listening port,
refused out-of-scope endpoints, and generated a valid generation-7 drain candidate.
When the port started listening, the checker generated a valid generation-8
re-enable candidate. No candidate was published. Temporary listeners stopped;
private artifacts remain at `/tmp/netforge-health-smoke-br7zuv_l` in netforge-live.
This demonstrates bounded active TCP reachability checks and candidate generation,
not autonomous health-to-publication operation or application-level health.

## Live related ICMP NAT acceptance — 2026-10-18

User ran `icmp-smoke.sh` with generation 9. The backend UDP port-unreachable
traversed reverse service/SNAT translation and reached the original client as
ECONNREFUSED via VIP 10.77.0.100:9000. Counters: received 5, emitted/TX sent 4,
drop 1 (`not_for_port`), ARP replies 2, new UDP mapping 1, related_icmp 1,
TX failures 0, RX oversize 0. Appliance stopped cleanly at the bounded timeout.
Recorded log: `outputs/netforge-live/icmp-smoke-1792331143-31866.log`.

## Linux scheduling-policy suite — 2026-10-20

User copied the current source to `/tmp/netforge-policy-rc1` in netforge-build
and ran its serial GCC sanitizer build with a 300-second timeout and build directory
`/tmp/netforge-policy-build`. All eight suites passed: Ethernet, IPv4, TCP,
scheduler, appliance, CLI, control and health. This verifies the new policies on
Linux aarch64, including deterministic weight cycles, least-connection selection,
rendezvous reorder/removal properties and drain exclusion. Live policy traffic
verification remains pending.

## Live service selection policies — 2026-10-20

User ran `policy-smoke.sh`. All 16 UDP probes passed: generation 10 weighted
selection A/A/A/B; generation 11 least tracked mappings A/B/A/B; generation 12
rendezvous B/B/A/A for source ports 35020/35021/35026/35028; generation 13 with
reversed backend order selected the same B/B/A/A. Each run created four mappings
and processed four reverse packets. TX sent was 8/10/8/8, failures zero in every
run; the least run also emitted two ARP replies. Backend replies A=9, B=7. All
bounded application/backend processes stopped. Logs are retained in
`outputs/netforge-live/policy-smoke-1792483778-43222`.
This proves this live UDP policy matrix, not broad distribution, TCP policy live
coverage, backend CPU balancing or performance improvement.

## Dynamic neighbor extension — native acceptance

Opt-in solicited discovery, bounded pending queues/cache, retries, negative expiry
and timer-driven reprocessing are implemented. All eight native suites pass with
new appliance cases: forward and return discovery/NAT, unsolicited/mismatched
replies, cache expiry, retry cadence, negative suppression/expiry, per-target and
target-count limits, global 256-KiB queue bound, eight-request rate bound, pending
deadline reclamation, separate disposition accounting and policy revalidation after
reload. Linux compilation of the updated AF_PACKET maintenance path and live ARP
integration remain pending. Existing live binaries do not include this extension.

## Linux dynamic-neighbor suite — 2026-10-20

User copied the current source to `/tmp/netforge-neighbor-rc1` in netforge-build
and ran the serial GCC sanitizer build with a 300-second timeout and build directory
`/tmp/netforge-neighbor-build`. All eight suites passed, including dynamic-neighbor
regressions and compilation of the updated AF_PACKET maintenance path. Live
solicited discovery/queued forwarding remains pending.

## Live dynamic next-hop resolution — 2026-10-20

User ran `neighbor-smoke.sh`, generation 14, without static neighbor entries. UDP
first/repeat and TCP service probes passed. Two solicited ARP replies populated
two cache entries; two original packets were deferred/reprocessed, and pending
targets/queued bytes/timeouts were zero at shutdown. Counters: engine received 22,
emitted 16, dropped 2 (arp_not_owned), deferred 2, consumed 2; ARP requests 2;
TX sent 18, TX failures 0, oversize 0; one UDP and one TCP mapping created. Backend
replies UDP=2 and TCP=1. All bounded processes stopped. Retained log:
`outputs/netforge-live/neighbor-smoke-1792484514-44043.log`.
Engine-emitted excludes two maintenance ARP queries; this explains TX 18 vs 16.
This proves the live discovery/cache/reprocessing path, not adversarial ARP
resistance or live overload/timeout scenarios.

## Linux health-controller suite — 2026-10-20

User copied source to `/tmp/netforge-healthctl-rc1` in netforge-build and ran the
serial GCC sanitizer build with a 300-second timeout, build directory
`/tmp/netforge-healthctl-build`. All nine suites passed, including health-to-API
publication/hysteresis, config drift, rejection and uncertain-state isolation.
Real combined health-controller/API/dataplane integration remains pending.

## Live acknowledged health reconciliation — 2026-10-20

User executed `health-controller-smoke.py` with actual TCP listeners, real loopback
HTTP and the actual AF_PACKET dataplane. First failure observation kept generation
6; repeated failure published a drain acknowledged at generation 7. First recovery
kept the drain without revision churn; repeated recovery published re-enable,
acknowledged at generation 8. An independent generation-9 publication invalidated
health state and stopped reconciliation. All assertions passed and the owned
appliance/listeners stopped cleanly. Private state remains at
`/tmp/netforge-health-controller-ixygv5tb` in netforge-live.
The accepting/closed ports were temporary container-local listeners, not application
traffic backends; combined health change plus end-client traffic/failure timing
and crash/restart reconciliation remain separate acceptance work.

## Environment capture — 2026-10-20

User ran the read-only collector and saved
`outputs/netforge-live/environment-20261020-140319.json`. Summary: 13.13 GiB free;
Docker 28.3.2/Linux aarch64, 2 CPUs, MemTotal 2,069,843,968 bytes, overlayfs,
cgroup v2; no collector issues. The collector did not change resource settings.

## Observability extension — native tests

Implemented engine byte/reprocessing accounting, flow expiry/high-water values,
aggregate ACL work/matches/default decisions, route lookup/miss counts, queue
high-water bytes and bounded per-port socket packet/byte/TX-failure series. Linux
snapshot code adds CPU/peak RSS/current resident/uptime observations, sampled only
when reporting. Native tests cover accounting, expiry uniqueness, label escaping
and metric values. Idle maintenance now reclaims expired flows even without new
packets; a regression checks idle expiry and prevents double counting. Updated
Linux adapter compilation and live metrics validation remain pending.

## Linux observability suite — 2026-10-20

User copied source to `/tmp/netforge-observability-rc1` in netforge-build and ran
the serial GCC sanitizer build with a 300-second timeout and build directory
`/tmp/netforge-observability-build`. All nine suites passed, including byte/expiry/
ACL/route accounting and bounded metric-label tests. Updated Linux AF_PACKET
resource/per-port reporting compiled successfully. Live metrics validation remains
pending.

## Live observability acceptance — 2026-10-20

User ran `observability-smoke.sh` via actual agent HTTP export. Two probes passed.
Core received 10/emitted 4/dropped 2/deferred 2/consumed 2; reprocessed packets 2.
Engine input bytes 530 minus reprocessed 120 equalled socket RX bytes 410; socket
RX packets 8 equalled engine received 10 minus retries 2. TX 6/326 bytes equalled
core emitted 4/242 bytes plus two 42-byte maintenance ARP queries. TX failures zero.
Flow high water 1, route lookups 6, ACL matches 2, neighbor queues/timeouts zero.
Process CPU/RSS/uptime metrics were present. Current RSS 1,789,952 bytes; peak
25,464,832 bytes may include Python fork/pre-exec history because Linux preserves
resource accounting across exec. It is not a steady-state dataplane RSS claim.
All bounded processes stopped. Retained log:
`outputs/netforge-live/observability-smoke-1792486875-46560.log`.

## Mixed-flow benchmark extension — native correctness

Implemented bounded warm/cold corpus processing and a repeated UDP workload harness.
Nine native suites passed with CLI warm/cold mapping/byte/argument regressions.
A correctness-only ASan/UBSan harness check passed 24 trials (1/4 flows, 64/128 bytes,
three repetitions, 100 warm iterations). Artifacts are under
`work/netforge-mixed-harness-check-20261020`; these are not optimized performance
results. Updated Linux validation and optimized baseline collection remain pending.

## Linux mixed-benchmark correctness — 2026-10-20

User ran the serial GCC sanitizer suite from `/tmp/netforge-benchmark-rc1`, build
`/tmp/netforge-benchmark-build`, timeout 300 seconds. All nine suites passed,
including new warm/cold corpus CLI regressions. Optimized mixed-flow collection
is prepared but has not yet run; no performance results are claimed.

## Optimized initial mixed-flow baseline — 2026-10-20

User ran `benchmark-baseline.sh`: 120 verified offline trials passed and were copied
to `outputs/netforge-live/baseline-20261020-163355-49822`. Build: GCC 13.3.0/Linux
aarch64 -O2 -static, no sanitizers, builder .5 CPU/512 MiB. For warm 64-byte inputs,
median core invocation rates were about 4.098M/s (1 flow), 1.348M/s (64) and
0.594M/s (256); larger-case variance was substantial. These are offline reference
measurements, not packet I/O, network throughput/latency or causal profiling.
The code contains linear lookup/expiry work; paired indexed experiments are
prepared to test that specific change without claiming improvement in advance.

## Indexed path native verification

Complete core suite passes in linear and indexed modes, including reverse NAT,
TCP phases, reloads, exhaustion/expiry, ARP reprocessing, partial expiry compaction,
NAT port reuse and saturated clocks. CLI replays agree between variants. New maps
are optional/default off and bounded by flow count. Updated Linux tests,
counterbalanced optimized timing and live indexed traffic remain pending.

## Full indexed performance/integration batch — 2026-10-20

User executed `performance-batch.sh`. All ten Linux sanitizer summaries passed,
including complete linear and indexed core suites. The same optimized binary
passed 240 counterbalanced offline trials; retained report is
`outputs/netforge-live/index-comparison-20261020-164120-51094`.
For warm 64-byte packets, median paired indexed/linear rates were 0.905 (1 flow),
2.043 (64 flows), 5.084 (256 flows). The small-table regression is retained;
indexing remains optional/default off. These are offline core observations under
CPU caps, not network throughput, p99 latency or universal speedup claims.

Generation 15 indexed live UDP first/repeat and TCP probes passed with dynamic
ARP. Final indexes forward/reverse/occupied=1/1/1 after TCP idle expiry; flow peak
2; two learned neighbors; zero queued bytes/timeouts/TX failures. TX 19 frames,
1186 bytes; current and peak process RSS 1,744,896 bytes when directly launched.
All bounded processes stopped. Live log:
`outputs/netforge-live/indexed-live-20261020-164120-51094.log`.

## XDP prerequisite results — 2026-10-20

User preflight: Linuxkit 6.10.14/aarch64, BTF exposed; CONFIG_BPF/BPF_SYSCALL/
BPF_JIT/XDP_SOCKETS/NET_CLS_BPF/NET_ACT_BPF enabled. Builder lacks clang/libbpf
pkg-config tools and effective BPF/NET_ADMIN capabilities, with seccomp active.
The initial simulated plan failed because bpftool has no package candidate.
The prepared implementation uses libbpf directly and omits bpftool; no package
installation has occurred during assistant-side work.

Native C++ sanitizer tests now pass the shared C XDP model's bounds, first-match
ACL, LPM, valid UDP reference byte parity, bad IPv4 checksum, ingress rules and
malformed corpus. Full native suite has eleven passing summaries. Kernel compiler/
verifier/test-run and all actual XDP attachment/network tests remain pending.

### XDP checker startup correction — 2026-10-20

The user-reported first isolated checker exited 255 before executing the test,
with `ensure /proc/thread-self/fd is on procfs: operation not permitted`;
OOMKilled was false. The custom default-deny seccomp profile omitted `fstatfs`.
The runc EnsureProcHandle implementation uses that syscall for the matching
procfs safety check (https://github.com/opencontainers/runc/blob/v1.2.0/libcontainer/utils/utils_unix.go).
Added only `fstatfs` to the allowlist. Kernel verification remains pending;
`../netforge-live/xdp-retry.sh` reuses retained compiled artifacts, creates one
network-isolated capped checker, stops it afterwards, and deletes nothing.

### XDP verifier bounds correction — 2026-10-20

The retry container started successfully after the fstatfs correction. The
kernel verifier rejected instruction 44, `R1 pointer -= pointer prohibited`,
at the packet-length subtraction. This is a program verification failure,
not evidence of missing capabilities. The BPF path now compares the bounded
IPv4 total-length packet pointer directly against data_end; the native model
retains its length subtraction. xdp-rebuild.sh reruns native sanitizer parity
using the previously built objects, rebuilds the BPF ELF/checker, and retries
the isolated kernel test without installing packages. Kernel acceptance
remains pending user execution. No interface attachment was attempted.

### XDP capability correction — 2026-10-20

The revised parser passed native sanitizer/reference tests. The kernel then
rejected its variable packet-pointer addition at instruction 43 with
`pointer arithmetic with it prohibited for !root`. Linux v6.10
include/linux/bpf.h gates bpf_bypass_spec_v1 on CAP_PERFMON; the initial checker
had only BPF and NET_ADMIN. Retry/batch now add PERFMON to the isolated checker
only, retaining default-deny seccomp (perf_event_open remains denied). The
retained artifact netforge-xdp-fix-20261020-173226-53888 can be retried without
compilation/downloads. No kernel acceptance or interface attachment claimed.

### XDP fixed header bounds preservation — 2026-10-20

With PERFMON, verification passed the variable packet-pointer addition. It
then rejected a UDP read at offset 39: only 34 packet bytes were proven safe.
The emitted instruction stream omitted the source-level fixed transport
bounds comparison after the dynamic total-length check. Added a BPF-only
empty inline assembly register barrier on data_end after that dynamic check
to prevent LLVM inferring away subsequent fixed bounds checks. Native model
semantics are unchanged. Kernel acceptance remains pending targeted rebuild.

### Initial kernel XDP acceptance — verified 2026-10-20

User executed xdp-rebuild.sh successfully. Native sanitizer parser/ACL/LPM,
reference byte parity and malformed corpus passed. Runtime bundle 1,418,888
bytes, artifacts netforge-xdp-fix-20261020-173601-54454; checker
netforge-xdp-retry-20261020-173609-54473 was stopped and retained.
Kernel verifier accepted nf_xdp. Six kernel test-runs passed: route action 4,
default deny 1, bad IPv4 checksum 1, TTL punt 2, fragment punt 2, ARP punt 2.
No interface attachment or live-frame test flag was used. This establishes
kernel acceptance of the static subset, not live forwarding or performance.

Next checker revision adds 50 truncated inputs (14..63 bytes) and 256 seeded
mutations, half with repaired IPv4 checksums, comparing actions against the
native model and routing bytes against reference output. Punt/drop output is
also checked for unchanged bytes. Total 312 kernel cases; execution pending.

### Expanded kernel XDP acceptance — verified 2026-10-20

User executed xdp-rebuild.sh: native sanitizer/reference tests passed, and
all 312 kernel cases passed (six fixtures, 50 truncations, 256 mutations).
Artifacts: netforge-xdp-fix-20261020-173856-54601, bundle 1,418,976 bytes.
Stopped checker retained: netforge-xdp-retry-20261020-173903-54613.
No interface attachment.

Prepared xdp-live.sh and live_check.c for a separate network-none capped
container: two veth pairs, generic XDP ingress attachment with no replacement,
three real redirects compared byte-for-byte with native output, interleaved
ACL/checksum egress-negative observations, owned-program detach verification.
Live acceptance is pending user execution. NET_RAW and socket syscalls are
added only to this dedicated live checker profile. No host interfaces, mounts,
network connections, package installs or existing lab changes are involved.

### Live XDP redirect acceptance — verified 2026-10-20

User ran xdp-live.sh successfully: three real private-veth redirects matched
native TTL/MAC/checksum bytes; ACL deny and bad IPv4 checksum were absent at
egress between positives; owned program detach and absence query passed.
Artifacts netforge-xdp-live-20261020-174319-54829, bundle 1,419,128 bytes.
Stopped network-none container of the same name retained. Generic XDP only;
no throughput, physical-NIC/native-driver, general config or stateful claims.

Next revision introduces nf-xdp-compile using the real C++ load_config/validate
path, explicit unique interface bindings, target-native 1044-byte ABI, bounded
UDP ACLs and static route next hops. Rejects NAT/services/TCP state/discovery/
flow indexing/default allow, non-UDP policies, unresolved neighbors, broad
direct routes and routes including owned IPs before opening output. Offline
compiler tests passed locally with Clang warnings-as-errors; live config
compilation plus forwarding acceptance is pending user execution. Full suite
now includes compiler tests in shell/CMake; full Linux suite not rerun yet.

### Live config binding correction — 2026-10-20

User execution passed Linux compiler tests and produced a 3.84-MB runtime
bundle, but the live shell printed interface indices 11 and 14 outside the
expected captured arguments; compilation rejected the binding before XDP
attachment. Replaced shell command substitution with explicit PORT=@DEVICE
resolution inside the compiler via if_nametoindex. The named device must match
the configured port device; unresolved names, zero and duplicate indices are
rejected. Numeric offline bindings remain supported. No seccomp/capability or
resource-limit expansion. Corrected live integration remains pending.

### Config-integrated generic XDP acceptance — verified 2026-10-20

User ran updated xdp-live.sh successfully. Static compiler rejection/ABI tests
passed. Shared project config compiled against actual namespace-local veth
indices, generation 1, policy_bytes 1044. Loaded policy drove all three real
redirects with exact native bytes; both negative observations and conditional
detach/absence checks passed. Artifacts/container:
netforge-xdp-live-20261020-174920-55239, 3.84-MB bundle including compiler.
The new container was stopped and retained. No existing topology was changed.

Prepared xdp-regression.sh: read-only environment/limit capture, stop on reported
issues or less than 10 GiB headroom, bounded full sanitizer suite in existing
builder, retain logs and source-manifest fingerprint. No new container, install,
interface attachment or deletion. Current full regression remains pending.

### Consolidated current-source regression — verified 2026-10-20

All twelve sanitizer/parser/core/compiler/CLI/control/health summary groups
passed in the existing capped Linux builder. Evidence directory:
../netforge-live/xdp-regression-20261020-175213-55411 (tests.log, environment.json,
source-manifest fingerprint). Docker 28.3.2, Linux aarch64, two CPUs,
2,069,843,968 bytes VM RAM; Mac free disk 11.83 GiB; environment issues empty.
No new containers, packages, network changes or deletions in this batch.

Prepared read-only dpdk_preflight.py for the next backend: installed-tool
inventory, CPU affinity, hugepage inventory and selected-version dependency
sizing from cached apt simulation. No installation or DPDK runtime acceptance
is implied. Actual preflight output remains pending.

### DPDK preflight and prepared bounded prototype — 2026-10-20

User preflight: aarch64/Linux 6.10.14-linuxkit, CPUs 0/1 allowed, no installed
DPDK, cc/pkg-config present, meson/ninja absent, zero allocated hugepages.
Full libdpdk-dev simulation succeeded: 232 packages, gross 208,203,776 bytes,
download 43,922,322 bytes; all selected-version metadata available. This full
plan was not installed. Builder virtual filesystem free bytes are not host
physical disk headroom.

Prepared a reduced guarded installer, extracted SDK build helper and actual
EAL/ethdev/ring-PMD prototype with bounded pool, 312 corpus inputs, explicit
partial-TX reclamation and cleanup checks. Python syntax and shell syntax
checked locally. DPDK C++ compilation, reduced actual plan, installation and
runtime acceptance remain pending manual dpdk-batch.sh execution.

### Initial actual DPDK runtime acceptance — verified 2026-10-20

User ran dpdk-batch.sh with 11.75 GiB Mac disk headroom. Restricted plan selected
19 runtime packages, estimated gross SDK+runtime 75.33 MiB, download 14.48 MiB.
Runtime installed and SDK extracted only in existing builder. DPDK 23.11.4
build/runtime passed EAL/ethdev/ring-PMD RX/TX, 312 policy-model corpus cases,
forced partial TX (7 sent/9 unsent), all 255 mbufs reclaimed after every batch,
stop/close/ring/pool release/EAL cleanup. Counters: RX328, route158, drop131,
punt39, sent149, unsent9. These are offline ring tests, not network throughput.
Evidence: ../netforge-live/dpdk-20261020-181000-56220/runtime.log; retained SDK:
/tmp/netforge-dpdk-20261020-181000-56220/sdk. No NIC/hugepages/new containers.

Next revision adds shared-parser config compilation, exact policy ABI checks
and 19 independent C++ Appliance route/deny comparisons (including sixteen
distinct valid source ports), retaining the shared-model 312-case and partial
TX checks. dpdk-integration.sh reuses existing SDK/runtime without installs.
This updated C++ build/runtime remains pending manual verification.

### Configured DPDK and independent reference acceptance — verified 2026-10-20

User dpdk-integration.sh passed at 11.64 GiB host headroom, with no installs:
DPDK 23.11.4 build, compiler rejection/ABI tests, generation-1 config policy,
312 ring-PMD corpus cases, nineteen independent C++ Appliance route/deny
comparisons, 7/9 partial-TX handling and all 255 mbufs reclaimed after each
batch. Counters RX328/route157/drop131/punt40/sent148/unsent9. Clean EAL/device/
ring/pool shutdown passed. Evidence ../netforge-live/
dpdk-integration-20261020-181441-56444; SDK from previous verified batch reused.

Prepared bounded WorkerPool UDP/SNAT adapter with one/two real owner threads,
partitioned PAT/state capacity, reverse dispatch, bounded FIFO admission and
draining shutdown. Initial native ASan/UBSan tests passed. Expanded concurrent
producer tests and Linux full thirteen-group regression/targeted TSan are
pending workers-batch.sh execution. No live worker I/O or scaling claim.

Native expanded worker rerun passed with warnings-as-errors, ASan/UBSan and
four concurrent producers. No sanitizer findings. Linux/TSan remains pending.

### Worker Linux and race acceptance — verified 2026-10-20

User workers-batch.sh passed all thirteen Linux sanitizer regression groups
and targeted ThreadSanitizer run, including four concurrent producers and
one/two-worker NAT ownership/queue/expiry/draining checks. Host headroom
11.61 GiB. Evidence ../netforge-live/workers-20261020-182224-56835.
No race report or sanitizer failure. No scale/throughput inference.

Next revision adds run-workers CONFIG 1|2 to AF_PACKET: main-thread socket
I/O, bounded ingress bursts and worker batches, per-owner results/emitted/drop
metrics, conditional immutable-worker HUP rejection and graceful stop. Worker
threads inherit blocked control signals; handlers execute on the socket thread.
Prepared sixteen private-veth UDP NAT request/reply probes and metrics assertions
using a new capped network-none container. Linux static compilation and actual
worker packet I/O remain pending workers-live.sh. Updated CLI/native thirteen
groups passed locally; native testing excludes Linux-only I/O code.

### Live two-worker UDP acceptance — verified 2026-10-20

User workers-live.sh passed at 11.47 GiB headroom. Static actual Linux CLI
compiled and ran in new network-none 128-MiB/.5-CPU/64-PID container. Sixteen
UDP frame round trips passed PAT uniqueness, reverse tuple/TTL/IP+UDP checksums.
Each owner emitted16 frames; successful TX32/failures0; resident2,072,576 bytes;
clean timeout stop. Container/artifacts workers-live-20261020-183414-57571
retained. RX44 total, including twelve owner-zero drops not attributed in that
revision. No IPv6 provenance is claimed for those twelve frames.

Next revision adds bounded unsupported/IPv6 counters and extends worker dispatch
to TCP, LAN-to-SNAT services (excluding global least-connections) and related
return ICMP quoted NAT ports. Extended native ASan/UBSan tests passed service
translation, TCP SYN/retransmission/bad-SYNACK/ACK/FIN and related ICMP ownership
against serial engines. Revised live probe adds four TCP service/FIN exchanges.
Combined workers-protocol-batch.sh Linux/race/live acceptance remains pending.
No new dependencies, host networking or unrelated resources are involved.

### Extended worker Linux/race pass; live probe correction — 2026-10-20

User combined protocol batch passed all thirteen Linux sanitizer groups and
ThreadSanitizer, including TCP/service/related-ICMP ownership. Evidence:
../netforge-live/workers-20261020-184546-58112. Live stage compiled and passed
sixteen UDP round trips, then the probe failed TCP/UDP PAT uniqueness at the
first TCP allocation. Evidence/runtime:
../netforge-live/workers-live-20261020-184857-58244. No TX/detach/complete TCP
acceptance was inferred from that failed run. Container was stopped by trap.

Verified in appliance.cpp: PAT allocation and occupied-port keys include the
IP protocol. Numeric port reuse between TCP and UDP is deliberate and safe
within the stated reverse-key model. The probe incorrectly treated them as
one namespace. Corrected probe to maintain separate TCP and UDP uniqueness
sets, still enforcing worker-partition range and stable established TCP ports.
No dataplane/concurrency code changed. Only live rerun is required; completed
Linux/TSan regression is not repeated for this assertion-only correction.

### Worker protocol live closure — verified 2026-10-20

User corrected workers-live.sh passed UDP16 round trips and four actual private
TCP service handshake/FIN frame exchanges. Each owner emitted28; TX56/failures0;
resident1,933,312 bytes; clean stop. Observed IPv6 RX12 and unsupported-owner-zero
drops12 explicitly recorded in this revision. This identifies the current run's
extra traffic; it does not retroactively attribute older drops. Host headroom
11.82 GiB. Evidence/container workers-live-20261020-185341-58462 retained/stopped.

Prepared opt-in DurableAgent with single-record fsynced checkpoint/intent journal,
exclusive writer lock, binary/integrity checks, stable credentials, monotonic
restart revisions and exact-child pidfd orphan termination. Unknown launch
identity refuses recovery. Native clean-restart/private-state/lock/corruption/
write-failure tests passed; Linux pidfd/crash and real HTTP/C++ acceptance are
pending durable-batch.sh. Existing Agent default behavior is unchanged.

## Durable control recovery — 2026-10-20

User-executed durable-batch.sh passed targeted original control regression and
durable tests: private state/writer lock, integrity, consumed rejected revisions,
lost ACK, fsync failure, exact-child pidfd termination and reused-PID safety.
Real authenticated HTTP acknowledged/checkpointed generation 7; SIGKILL of the
owned supervisor followed by explicit orphan recovery restored semantics at 8.
Stale and immutable-limit rejection retained the checkpoint; clean restart
advanced past rejected 9 to 10, with fresh metrics and zero TX failures.
Evidence: netforge-live/durable-20261020-191046-59303. Test-created processes
stopped; private state retained. Configuration recovery does not retain flows.

## Journaled fleet acceptance — 2026-10-20

User-executed fleet-batch.sh passed original control and journal regressions.
Two logical durable agents with actual C++ children passed whole-fleet stale
preflight/no publication, partial runtime rejection and consumed revision,
explicit corrected plan to generation 8, then injected response loss after real
publication and reopened-controller exact hash/generation reconciliation to 9.
No duplicate POST; repeated completed reconciliation was idempotent. Both real
metric snapshots reported generation 9 and zero TX failures; owned processes
stopped. Evidence: netforge-live/fleet-20261020-191906-59715. Same-container
logical agents do not establish independent failure domains or consensus.

## Worker idle maintenance acceptance — 2026-10-20

User-executed workers-idle-batch.sh passed the consolidated fifteen-group Linux
ASan/UBSan regression and targeted worker ThreadSanitizer run. Evidence:
netforge-live/workers-20261020-192350-60052. Private-veth live acceptance passed
sixteen UDP NAT round trips and four TCP service/FIN exchanges, TX56/failures0,
with ten mappings expired on each owner and zero flows retained during idle time.
Resident bytes2,011,136; IPv6 unsupported observations12. Clean bounded shutdown;
stopped test container retained. Live evidence:
netforge-live/workers-live-20261020-192703-60178. Free disk11.38 GiB at live start.
This is correctness/resource evidence, not a throughput benchmark.

## Worker reload regression — 2026-10-20

User-executed workers-reload-batch.sh passed all fifteen Linux sanitizer groups
and targeted worker ThreadSanitizer: workers-20261020-193420-60601. The following
live harness stopped before engine startup because the capability-restricted
process could not create a PID file in the copied UID501-owned runtime directory.
No live reload result is claimed. Harness PID storage was moved to container /tmp;
the already-passed regression does not need repeating for this harness-only fix.

The first harness retry also stopped before engine startup with shell echo I/O
error. The restrictive profile omits dup/dup2/dup3; shell redirection is therefore
not a supported harness mechanism. Startup/config publication now uses native
probe --launch/--update modes with direct bounded file I/O, owned-executable PID
validation and exec/SIGHUP. No syscall/capability expansion was introduced.
This retry still requires live acceptance; no engine failure or successful
reload is inferred from either pre-start harness failure.

## Worker live transactional reload — 2026-10-20

User-executed corrected workers-live.sh passed native-launcher startup, sixteen
UDP round trips and four TCP service/FIN exchanges, followed by generation2 deny
publication, retained UDP rounds/new-flow denial, incompatible generation3
rejection and repeated continuity/deny checks. Final generation2; TX120/failures0;
sixty emissions and ten retained mappings per owner, RSS2,142,208 bytes. Runtime
stopped cleanly and private container retained. Evidence:
netforge-live/workers-live-20261020-195900-61662. Mac headroom11.16 GiB.
This completes the live reload gate that the prior harness failures did not reach.

## XDP expected-owner replacement and rollback — 2026-11-06

User-executed xdp-live.sh passed config ABI/bindings checks, real generic-veth
TTL/MAC/checksum byte parity, ACL/checksum negatives, wrong expected-owner
replacement rejection with original forwarding retained, prepared frozen deny
policy replacement with live drop, conditional rollback restoring the exact
original program ID and forwarding, and owned detach/absence verification.
The libbpf active-program mismatch message was expected for the negative test.
Evidence artifact: netforge-live/netforge-xdp-live-20261106-101936-13149; stopped
container netforge-xdp-live-20261106-101936-13149 retained. Free Mac disk15.45 GiB.
This verifies in-process lifecycle ownership, not API/durable XDP management.

Subsequent per-CPU kernel decision-counter implementation is pending the combined
312-case kernel and live-counter xdp-live.sh gate. Previous lifecycle acceptance
does not establish correctness of the changed BPF program.

## XDP kernel counters and lifecycle — 2026-11-06

User-executed extended xdp-live.sh passed real kernel verifier acceptance and all
312 cases, compiled configuration binding, real generic-veth parity/negative
traffic, ownership-rejected replacement, immutable candidate replacement, exact
program rollback and conditional detach/absence verification. Per-CPU sums:
original ROUTE5/320 bytes, DROP2/128; replacement ROUTE0/0, DROP1/64. Retired
object statistics remained readable while its FD was held. These are decision
counters, not successful-delivery counts. Artifact:
netforge-live/netforge-xdp-live-20261106-102757-13885. Stopped private container
retained; Mac headroom14.47 GiB at batch start. API lifecycle/metrics publication
remains unfinished.

## Release CMake/CTest acceptance — 2026-11-06

User-executed cmake-release-batch.sh passed actual CMake3.28.3 configuration,
serial GCC13.3 Release compilation and all15 CTest tests, including durable,
workers, fleet and health controller. CTest reported8.22 seconds; CLI version
0.1.0-rc1 and example config validation passed. Seven builder-only tool packages
were installed after bounded planning (44.5 MiB gross/12.7 MiB download).
Evidence: netforge-live/cmake-20261106-103108-14093. Free Mac disk14.32 GiB.
Hosted CI, fresh-machine reproduction and final source/release packaging remain
separate gates. Subsequent worker API changes require the expanded16-test gate.

## Worker API integration — 2026-11-06

User-executed worker-api-batch.sh passed serial CMake Release compilation and
all16 CTest tests (8.75 seconds), real worker checker, sixteen UDP round trips
and four TCP service/FIN exchanges, authenticated HTTP publication of generation2,
old-flow continuation, new-flow denial, runtime limit rejection/file restoration,
unsupported-worker and stale-publisher rejection, API metrics TX120/failures0,
and owned-process shutdown. Evidence: netforge-live/worker-api-20261106-103705-14835.
Cached runtime39 MB; free Mac disk13.91 GiB at start. No durable-worker recovery
is claimed. Later audit-only changes reuse this C++ binary but require Python
control regression and real HTTP audit acceptance.

## Bounded publication audit acceptance — 2026-11-06

User-executed audit-batch.sh passed original control regression, Linux durable
ACK/write-failure/pidfd/lost-ACK/reused-PID tests, worker adapter regression,
in-memory response-disconnect invariants and authenticated HTTP audit bounds,
correlation/header/hash/outcome/eviction/uncertainty/privacy checks. Real worker
HTTP/packet replay passed generation2 publication, runtime rejection/restoration,
unsupported/stale refusal, retained traffic and audit hashes/outcomes, with
TX120/failures0 and clean owned shutdown. Evidence:
netforge-live/audit-20261106-104415-15619. Cached C++/Python runtime reused; private
container stopped and retained; Mac disk12.61 GiB. No durable audit-log claim.

## PCAP parity diagnosis and reference checksum repair — 2026-11-06

User-executed dpdk-pcap-batch.sh passed actual DPDK build, sanitizer PCAP I/O,
compiled policy,312 corpus cases,19 reference comparisons, forced partial TX
and255-mbuf reclamation, but failed the new capture/reference comparison.
Retained stage: /tmp/netforge-dpdk-pcap-20261106-105352-16445. No PCAP acceptance
pass is claimed. Mac headroom12.45 GiB.

Native reproduction with the same input generator and shared policy found only
UDP checksum bytes40/41 differed for routed frames at64/128/512/1514 bytes: input
checksums were zero; the reference filled them despite unchanged IP/port tuples.
The reference now preserves absent IPv4 UDP checksums for untranslated routing.
Translated tuples retain the existing checksum-repair behavior. Regression covers
new/tracked untranslated flows, enabled checksum preservation, TTL/IP repair and
NAT from a zero-checksum input, in both linear/indexed appliance tests.

Native ASan/UBSan Ethernet/IPv4/TCP/scheduler, both appliance paths, workers and
XDP model tests passed; CLI/control/clean durable/worker adapter tests passed.
Linux pidfd tests were skipped natively. The repaired reference matches the native
shared policy byte-for-byte for every common capture fixture. Linux full17-test
Release and actual DPDK capture/shutdown acceptance remain pending
checksum-reference-batch.sh. Historical measurements/artifacts remain unchanged.

## Reference repair and DPDK PCAP acceptance — 2026-11-06

User-executed checksum-reference-batch.sh passed serial CMake Release build and
all17 CTest tests (8.77 seconds), including the checksum regression, worker/API,
durability, fleet, compiler and audit. Reference stage:
/tmp/netforge-checksum-20261106-105900-17225; host evidence:
netforge-live/checksum-20261106-105900-17225.

Actual DPDK acceptance then passed the312-case corpus,19 independent reference
checks and forced TX7/unsent9 with all255 mbufs reclaimed. External PCAP gate
passed163 records:64 byte-exact reference routes at four sizes,96 drops,three
original punts with timestamps, empty input,ten malformed/budget cases, existing
output refusal and in-flight SIGTERM with valid incomplete output and EAL cleanup.
Evidence: netforge-live/dpdk-pcap-20261106-105952-17272; captures retained under
/tmp/netforge-dpdk-pcap-20261106-105952-17272/cases. Mac headroom12.31 GiB.
No continuous NIC service, successful-delivery benchmark or DPDK management API
is inferred from this bounded capture adapter.


XDP HTTP acceptance passed on 2026-11-06 in
netforge-live/xdp-api-20261106-111609-18440: authenticated startup, second-owner
refusal, generation2 deny, higher-revision restore, runtime/preflight/stale
rejection, continued byte parity, program-labelled counters, bounded audit and
owned detach with verified absence. This supersedes the pending API gate above.
No durable XDP recovery or physical NIC performance is claimed.


Prepared tcp-stream-batch.sh: two bounded persistent TCP sessions through the
reference dataplane, 256 KiB direct SNAT and 1 MiB service translation with
deterministic binary payloads, exact echo comparisons, client half-close and EOF.
Native strict-warning syntax check and shell syntax check passed. Actual Linux
execution is pending; this is a correctness gate, not a throughput benchmark.


## Persistent TCP acceptance — 2026-11-06

User-executed tcp-stream-batch.sh passed in
netforge-live/tcp-stream-20261106-112110-19055. Direct SNAT transferred262144
bytes over64 exchanges; service translation transferred1048576 bytes over128
exchanges. Deterministic binary payloads matched, both client half-closes reached
EOF, and the engine stopped cleanly. TX2572/failures0; two TCP flows expired;
resident memory1880064 bytes. This is bounded data-path correctness evidence,
not a throughput, sustained-load, loss-recovery or TCP window-validation result.


## Expanded fault closure — prepared 2026-11-06

Both linear/indexed appliance suites passed native ASan/UBSan after adding:
256 rejected PAT allocations preserving the admitted mapping; exact port reuse
after expiry; all-backend drain followed by recovery while old affinity remains;
all28 undersized ICMP quotes, corrupt outer/quoted checksums and no lifetime
refresh. These deterministic tests do not simulate real network loss or reordering.
integration-closure-batch.sh prepares one serial full Linux Release/CTest gate
in the existing512 MiB/.5 CPU builder; execution pending. No dependencies added.


## Integrated closure regression — 2026-11-06

User-executed integration-closure-batch.sh passed GCC13.3/CMake Release build
and all17 CTest tests in9.22 seconds. It includes expanded fault checks, shared
XDP compiler, workers, API, durable recovery, fleet, health and audit. Evidence:
netforge-live/integration-closure-20261106-112426-19612. Build stage:
/tmp/netforge-closure-20261106-112426-19612. This does not replace the separate
real-kernel/live backend gates or establish full-roadmap completion.

Offline evidence_report.py generated evidence-report-20261106-closure with
SHA256-linked retained logs and historical offline measurements. Its markers
are an index of evidence, not a new execution or general release certification.


Offline metrics view implemented and tested locally on2026-11-06: bounded input
and series, malformed/nonfinite/negative values, duplicate snapshots and HTML
escaping checked; actual retained TCP snapshot rendered. New CTest registration
is metrics_view;18-test Linux execution is not yet claimed. Engineering report
now distinguishes historical offline timings from live functional evidence.


## Worker dispatch experiment — prepared 2026-11-06

worker_bench.cpp checks warm serial-shard byte parity,16 unique PAT ports and
both owner coverage before timing. Every timed result verifies owner, tracked
decision, egress and exact bytes. compare_workers.py runs12 counterbalanced pairs
(one/two workers),8000 invocations/trial and saves all trials, paired medians,
descriptive bootstrap interval and Linux cgroup CPU accounting when available.
Native ASan/UBSan benchmark and all24 harness trials passed; native sanitizer
timing is not an optimized Linux performance result. Linux execution pending
worker-comparison-batch.sh, which reuses source-checked integrated core/worker
archives, preserves builder limits and installs nothing.


## Linux worker dispatch comparison — 2026-11-06

All24 verified counterbalanced trials passed in
netforge-live/worker-comparison-20261106-113348-20538. Paired median two/one
rate ratio0.99930678475; descriptive95% bootstrap interval[0.36717,2.51077].
This wide interval provides no evidence of a worker speedup. During the run,
10 additional CFS periods were throttled and throttled time rose404815 usec.
The half-CPU cap is retained. This measures bounded offline dispatch with byte
verification, not dedicated-core scaling, network throughput or delivery.


Prepared source-reproduction-batch.sh: bounded manifest-verified source archive
extracted into a new directory, no cached objects, serial Release build and
18 CTest tests in the existing capped builder. No dependency installation or
lab networking changes. Fresh-directory reproduction is not fresh-machine/CI
execution. Linux run pending.


## Source archive reproduction — 2026-11-06

User-executed source-reproduction-batch.sh verified/extracted123 manifest entries
and rebuilt from scratch with GCC13.3. All18 CTest tests passed in9.16 seconds.
Evidence:netforge-live/source-reproduction-20261106-113624-21020. Source archive
SHA256 ac5b3bc364303dc9b9e3fcef657c221fb812660d5530cb5628e0847379d13fd2.
Existing Linux toolchain reused; no clean-machine or hosted CI claim.

Subsequent ACL diagnostics change passed both linear/indexed native ASan/UBSan
suites: ordered evaluation/hits, established bypass, generation reset, and16-rule
export cap with omitted count. Full Linux compilation and integrated tests for
this change are pending rerun of source-reproduction-batch.sh.


## ACL diagnostics integrated reproduction — 2026-11-06

source-reproduction-20261106-113932-21544 passed archive verification, fresh
GCC13.3 Release build and18/18 CTest tests in9.10 seconds. Archive SHA256:
7d6ae259f5abae40f8e5e0f9d6cc888455fc1f5c538ab09434816ab6f6f5ecef.
Build cache:/tmp/netforge-reproduce-20261106-113932-21544. This verifies the
new bounded rule counters in the integrated core/worker build.

tcp-stream-batch.sh now uses this source-checked core archive and additionally
requires exactly one hit each for configured direct/service TCP rules, no omitted
rule series, and successful bounded snapshot rendering. Live execution pending.


## Live rule diagnostics — 2026-11-06

User-executed tcp-stream-batch.sh passed in tcp-stream-20261106-114153-22005:
256 KiB direct/1 MiB service payloads, half-close/EOF,2535 TX/zero failures.
Rules5/6 each had one hit;2530 tracked TCP packets bypassed ACL evaluation.
No omitted rules, snapshot rendered, two flows expired; resident1916928 bytes.

Subsequent portable XDP policy interchange changes passed native ASan/UBSan
compiler tests: independent big-endian field oracle, native ABI round-trip, all69
truncation lengths, trailing data, unknown version, oversized/count/field mutations
and destination preservation on malformed input. Linux focused execution is
pending policy-format-batch.sh. BPF program/map ABI unchanged.


## Portable policy Linux acceptance — 2026-11-06

policy-format-20261106-114532-22754 passed focused GCC compilation, expanded
compiler rejection/byte-oracle corpus,69-byte interchange export,1044-byte native
conversion and exact equality with direct native compilation. Neither BPF program
nor map ABI changed and no kernel program was loaded by this gate.

Prepared fleet-outage-batch.sh extends the existing actual HTTP/durable-agent
acceptance: a closed member endpoint must stop full-fleet preflight; endpoint
loss after peer ACK must retain partial journal state; same-identity endpoint
rejoin must converge without repeating the already-applied POST. It uses only
the existing128 MiB/.5CPU live container and verified stream engine. Endpoint
availability is interrupted, not the dataplane process; no host-failover claim.
Shell/Python syntax checks passed; actual execution pending.


## Fleet outage test correction — 2026-11-06

The first fleet-outage run passed stale/partial/lost-response gates and real
unavailable-endpoint preflight, then failed because the test incorrectly expected
automatic retry of an unapplied intended revision. The controller correctly
refused it with `requires a new operator-reviewed plan`; no controller change.
The smoke now asserts that refusal with no new POST, then supplies an explicit
corrective generation11 plan from current10/9 and checks idempotent convergence.
A deterministic fleet_journal_test regression for pre-apply connection refusal
and no blind retry passed locally. Corrected actual HTTP execution remains pending.


## Fleet endpoint outage/rejoin acceptance — 2026-11-06

Corrected user-executed fleet-outage-batch.sh passed in
netforge-live/fleet-outage-20261106-114929-23649. Actual unavailable endpoint
blocked full-fleet publication; mid-rollout loss left revisions10/9 and retained
verified/intended journal states. Rejoin refused blind replay without another
POST. An explicit generation11 corrective plan converged both agents; completed
reconciliation issued no duplicate POST. Both real children reported generation11
and zero TX failures and stopped cleanly. Private state retained at
/tmp/netforge-fleet-live-b6_c_kpn. This covers local HTTP endpoint availability,
not separate hosts, node membership, consensus or process/flow failover.


Prepared health-fault-batch.sh extends real health/API acceptance with a single
failure/recovery flap (no revision churn), two failed observations of both
endpoints (drain generation9), two recovery observations (enable generation10)
and external generation11 drift refusal. Only temporary listeners and an agent
in the existing capped live container are used. Shell/Python syntax checks
passed; actual execution pending. No data-traffic failover claim is inferred
from TCP reachability observations.


## Health fault acceptance — 2026-11-06

health-fault-20261106-115126-24102 passed real health-to-HTTP/C++ acceptance:
single-observation flap produced no revision, repeated all-backend failure drained
both TCP endpoints at9, repeated recovery re-enabled at10, external revision11
stopped reconciliation; clean shutdown. Private state retained at
/tmp/netforge-health-controller-hlp3z84p. These are reachability/control tests,
not packet-loss or seamless failover measurements.

Added run_demo.py/DEMO.md: five bounded synthetic replay cases with independent
IP/UDP/ICMP checksum, NAT tuple, service affinity and reason checks. Native local
CLI invocation passed; actual verified Linux operator-demo-batch.sh pending.
No core changes and no full-regression repeat is required for this demo helper.


## Operator demo Linux acceptance — 2026-11-06

operator-demo-20261106-115317-24549 passed all five synthetic cases against
the source-checked verified Linux binary. Inputs, reason outputs, independent
checksum checks, binary/config hashes and metrics retained. No live socket claim.

Prepared scoped lab.py plan/up/status/stop with ownership labels, collisions,
internal network, local image ID pinning and unchanged resource caps; no deletion
action. Local plan tests passed read-only behavior and container/subnet collision
refusal. Actual Docker plan and creation have not run. See LAB-SETUP.md.


## Owned lab provisioning — 2026-11-06

User ran read-only lab.py plan and explicit up successfully. New internal
netforge-repro-net10.79.0.0/24 with netforge-repro-a/b/app, each128 MiB/no swap,
.5CPU/64PIDs. Local imageID f3478af93fee5da7ca18b17e55e3abc90889a7c931717aaf59f9a3e82d7eac6c.
Output:netforge-live/repro-lab-20261106/appliance.conf and plan.json.
Existing original lab untouched. Actual new-lab UDP acceptance is prepared in
repro-acceptance-batch.sh; ownership/limits verified before staging verified
static engine/helper, bounded processes only, no build or route changes.


## Newly provisioned lab live acceptance — 2026-11-06

repro-acceptance-20261106-120019-25751 passed direct UDP SNAT, round-robin
backend selection and repeat-flow affinity on the new labeled10.79.0.0/24 lab.
Backend replies3/1, three mappings, TX11/failures0, drops0, clean bounded stop,
resident1916928 bytes. Ownership/resource checks passed before staging.
Existing original lab was not used or modified by this batch. Mac headroom
10.38 GiB: avoid additional build/dependency/container expansion near10-GiB guard.
This is reproduction from the prepared local image, not fresh-image bootstrap.


Health controller now writes bounded per-endpoint reachability/streak gauges
and accepted generation/publication indicators after successful reconciliation.
Native controller tests passed fail/drain/recovery metrics and no snapshot on
rejected/uncertain publication. No dependencies/containers added. Actual Linux
metrics export remains pending; previously verified live health behavior unchanged.
Local source/evidence/work footprint observed about291 MiB; Docker builder
inventory is needed before selecting any approved cleanup near10-GiB guard.


## Approved obsolete-build cleanup and disk guard — 2026-11-06

User explicitly approved the exact11-directory list in
netforge-live/cleanup-reviewed-builds.sh and executed it successfully. All11
REMOVED confirmations and DONE were returned. No container/image/volume deletion
was requested or performed. Historical build paths in this list are no longer
available; retained source snapshots/host evidence and current required caches
remain. Estimated473 MiB was directory usage, not a guarantee of host reclamation.
Subsequent user df reported9.8 GiB available on /System/Volumes/Data (96% used),
below the10-GiB guard. Cause of the host-space change is not established.
Dependency/build/container expansion is paused until sufficient disk is available.
New health metrics Linux acceptance remains pending.


User read-only precision check subsequently reported10.31 GiB free and guard
PASS. Only no-build/no-install bounded health acceptance is resumed; guard is
rechecked by health-fault-batch.sh. Expanded smoke verifies real metrics.prom
for generation9 all-down and10 recovery, per-endpoint streak2, flap publication0
and exactly9 series for two endpoints. Execution pending; no new Docker resources.


## Real health metrics acceptance — 2026-11-06

health-fault-20261106-121322-27458 passed actual health probe/export/API checks:
both endpoint probe_up1/success_streak2/failure_streak0, accepted generation10,
published1 and9 total series. Outage generation9 and no-publication flap gauges
also verified, external drift stopped reconciliation and shutdown was clean.
Private state:/tmp/netforge-health-controller-eur1kofo; Mac headroom10.22 GiB.

Prepared loss-recovery-batch.sh uses only the owned repro client eth0: requires
original noqueue, adds unique netem handlef123 with100% loss, checks probe timeout
and actual qdisc drop count, removes only the expected test handle with EXIT
cleanup, verifies original noqueue, existing/new flow recovery and two mappings.
Existing verified engine/helper, no build/install/new containers. Syntax checked;
actual kernel netem availability and live fault acceptance pending.


## Controlled real UDP loss/recovery — 2026-11-06

loss-recovery-20261106-121527-27915 passed on the labeled repro client only:
netem100% loss recorded one dropped packet and probe timeout; ownedf123 qdisc
removed and default noqueue verified. Existing flow and new flow recovered,
backend replies3, mappings2, TX8/failures0 and clean stop. Engine not_for_port1
is distinct from the injected kernel drop. Mac headroom10.20 GiB. This is a
deterministic complete-loss interval, not stochastic reordering/latency evidence.

Hosted CI workflow now includes local-image provisioning plan safety tests,
manifest-verified package/unpack, offline demo and3-day bounded artifacts in
addition to native sanitizer CMake/CTest on Linux/macOS. Workflow source prepared;
remote execution is unverified and awaits a user-selected repository.


GitHub repository UtkarshAlshi/netforge selected by user for hosted CI. Inspected
main at e413c8b543c70449703ba557d1a91dab6edd03f0 (M0–M1 foundation). Original
parser/tests/tools/lab/design notes preserved locally for integration; original
architecture retained in docs/architecture-m1.md. CMake now includes the legacy
parser test alongside18 current tests. Hosted execution and review-branch
publication are pending; main has not been changed.


Hosted Ubuntu job127759559901 passed all19 CTest tests plus provisioning-plan
safety, source package/extraction, operator demo and artifact upload on commit
2da6746977071aa3c549e469e4fe2242d2d0e542 (run42177602248). Initial macOS14 jobs
were cancelled before steps: GitHub retired that image on2026-11-02. Workflow
updated to supported macOS15 and PR-only branch testing (push also runs onmain)
to avoid duplicate branch/PR jobs. New cross-platform run pending. DraftPR1;
main unchanged.


Prepared DPDK general capture-policy integration: exact configuration-to-policy
validation before EAL, multiple supported ACL/routes for capture mode, fixed
fixture restriction only for corpus mode. Native ASan/UBSan test passed valid
multiple-policy binding, config/blob mismatch, tampered MAC/action and unsupported
SNAT/return-egress rejection. New independent actual DPDK test prepares three
rules/two routes, four packets, byte/timestamp comparison with reference and
pre-EAL blob/config mismatch refusal. Expanded DPDK batch execution pending;
existing SDK/latest source-matched core reused, no dependency install/container.
CMake adds dpdk_policy_binding (20 tests total); hosted execution not yet run
for these new changes.


Latest hosted CI run42178801073 completed successfully for DPDK binding commit
e6a9ae9a7d019b117caf7f6febb45477cce0a8b0. Local actual-DPDK batch stopped at
8.89 GiB free before compiling/running; actual multi-rule/multi-route DPDK
acceptance remains unverified. Native-policy tests in hosted CI do not close
that runtime gate. DraftPR1 remains the integration review, not a merged release.


Prepared hosted dpdk-runtime job to remove the local disk blocker from actual
DPDK verification. It creates a fresh Ubuntu24.04 container on a GitHub runner,
not the Mac, capped512 MiB/no swap,.5CPU/128PIDs. Source is manifest-packaged;
restricted DPDK runtime/SDK, serial Release build,20 CTest, actual corpus/PCAP
and multiple-policy acceptance are executed there. Dependencies/evidence remain
on the ephemeral runner with3-day artifact retention. Shell syntax checked;
hosted runtime execution pending. No local Docker command has been run.


First hosted DPDK run42221546892 passed native CI but failed x86 compilation:
rte_memcpy SSSE3 intrinsics needed the packaged x86 CPU baseline. build_dpdk_ring
now adds -march=corei7 for x86_64 only; ARM unchanged. This matches DPDK's generic
x86 baseline (official23.11 build guide); no -march=native optimization is used.
Rerun pending.

Added --check to actual DPDK runtime (policy/config validation before EAL) and
optional loopback capture supervisor. One fixed input/config, four jobs, pidfd
signals, bounded logs/deadline and idempotent request IDs; finite PCAP jobs only.
Linux lifecycle tests and real HTTP/DPDK acceptance are prepared in hosted CI.
Python/shell syntax checks passed; these new lifecycle paths are not yet verified.
