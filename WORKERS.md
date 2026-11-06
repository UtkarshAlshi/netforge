# Bounded UDP/TCP/SNAT workers

One/two-worker UDP ownership passed native/Linux ASan/UBSan, Linux ThreadSanitizer
and real AF_PACKET private-veth round trips. TCP/service/related-ICMP extensions passed native/Linux ASan/UBSan and Linux
ThreadSanitizer. Live UDP/TCP integration also passed on 2026-10-20 after correcting protocol-
specific PAT uniqueness in the probe. No
throughput/scaling claim is made.

## Ownership and resource contract

Each worker exclusively owns a reference Appliance. Stable original IPv4/port/
protocol hashing selects the forward owner. Translated replies use destination
NAT IP and disjoint PAT ranges to recover that owner; engine reverse-key/TCP
checks still validate the packet. Related return ICMP errors select the quoted
NAT source port's owner, then receive full engine validation. Stateless ICMP and
ARP are processed on worker zero. Options/fragments/unsupported protocols are
explicitly rejected by dispatch.

The adapter requires two ports, SNAT and static neighbors. Total PAT range and
flow quota are partitioned, not multiplied, across 1..2 workers. A busy worker
cannot borrow another's unused quota. TCP tracking is opt-in as in the reference
engine. Services must enter from LAN and have backend routes through the SNAT
port. Round-robin/weighted scheduling is local to each shard, not a globally
ordered cycle. Rendezvous remains tuple-derived. Global least-connections is
rejected rather than silently replaced with per-shard least load. Dynamic
neighbor discovery remains unsupported because cache/pending reply ownership
has not been implemented across workers.

Per-worker FIFO queues hold 1..64 jobs plus one executing job. Batches are at
most64, frames at most9014 bytes. Admission locks bound memory and ensure
deterministic queue-full rejection with completed futures. Concurrent producers
are serialized only for dispatch/admission; each engine remains thread-owned.
Shutdown closes admission, drains admitted jobs and joins owned threads.
Exceptions propagate per-job; future submissions return worker_pool_closed.

## AF_PACKET CLI

`netforge run-workers CONFIG 1|2` keeps socket RX/TX on the main thread, collects
at most16 frames per ready port (32 per batch) and drains worker futures before
the next poll. It is a batch barrier design, not overlapped I/O. Thread creation
inherits blocked control signals; only the socket thread restores those signals
after handlers are installed. No CPU affinity or extra quota is requested.

SIGHUP stages and publishes compatible configurations while preserving pinned flow state. The
management agent continues to use the original single-engine run command.
Worker mode reports bounded per-owner result/emitted/drop/unsupported counters,
IPv6 RX observations, per-port wire counts and process resources; it does not
read worker engine counters concurrently. IPv6 is unsupported.

## Evidence and tests

Initial live test passed sixteen UDP NAT round trips: both owners emitted16
frames each, TX32/failures0, resident2,072,576 bytes, clean shutdown. Twelve other
received frames were dropped on owner zero; that run did not record their
ethertype/reason. Do not infer their provenance. New counters make subsequent
unsupported/IPv6 traffic visible.

Tests compare one/two-worker bytes/decisions to serial shard engines, verify
unique PAT, reverse ownership, expiry, aggregate exhaustion, exact queue
backpressure, bounds, four concurrent producers and draining shutdown. Extended
tests add UDP/TCP service tuples, SYN retransmission, invalid SYNACK rejection,
ACK/FIN exchanges and quoted-ICMP owner dispatch on both workers.

workers-protocol-batch.sh combines full regression, targeted TSan and live
AF_PACKET checks. The revised live probe sends sixteen UDP round trips plus four
TCP service handshakes/FIN exchanges (synthetic valid frames, not TCP endpoint
sockets). Expected successful TX56, with28 emissions from each owner; exact
wire tuples/checksums and live PAT ownership are checked.

## Remaining gates

Dynamic neighbor ownership, global least-load
scheduling, XDP/DPDK worker dispatch, worker crash/
restart recovery and measured one-vs-two-worker throughput remain unfinished.
Serial-shard parity does not imply identical allocation/quotas to a single
unpartitioned appliance. TCP retains the reference engine's stated sequence/
window-validation limitations. No seamless flow failover or production claim.

PAT uniqueness is per IP protocol, NAT address and numeric port. TCP and UDP
may legitimately share a numeric port; worker partitions remain disjoint
within each protocol. The initial extended live probe incorrectly rejected
this permitted reuse. It now checks separate protocol namespaces.

Latest live result:16 UDP round trips and four TCP service/FIN exchanges, each
owner emitted28, TX56/failures0, resident1,933,312 bytes. Current IPv6 RX12 and
unsupported drops12 were observed explicitly. Related ICMP ownership has
native/Linux tests; this live probe does not inject ICMP errors.

## Idle maintenance extension

The main AF_PACKET loop now submits an owner-local FIFO maintenance barrier at
most every 250 ms (under load the synchronous loop may delay it). It never reads
engine state concurrently: each owner returns flow count and cumulative expiry
with its maintenance decisions. Metrics expose last completed maintenance
snapshots as netforge_worker_flows and netforge_worker_flows_expired_total.
Queued plus executing work remains bounded; shutdown serializes with maintenance
admission and drains owned jobs. Static-neighbor mode normally emits no maintenance
frames. Dynamic discovery remains rejected.

Native ASan/UBSan tests passed one/two-owner idle expiry without packet arrival,
pre-expiry preservation, no double-counting, PAT reuse and closed-pool refusal.
workers-idle-batch.sh prepares full Linux regression/TSan plus real private-veth
UDP/TCP acceptance with test-only 3-second idle/closing timeouts, then verifies
all twenty mappings expire before shutdown. Linux regression/TSan and live acceptance passed on 2026-10-20: all twenty mappings expired, ten per owner, with zero retained flows and TX failures.

## Transactional reload extension

Reload pauses new admissions, drains prior work through FIFO owner control jobs,
and copies/reloads each owner engine into a staged snapshot. Only after every
owner accepts does the coordinator lock the quiescent queues and swap all engine
pointers/configs with noexcept operations. A later-owner validation or allocation
failure leaves every published owner unchanged. Engine methods still execute
only on their owner threads. Existing flows keep their prior configuration and
PAT state; new flows use the new revision. PAT address/port/range changes require
restart to preserve deterministic reverse ownership. Other reference immutable
limits and the eight pinned-generation bound remain enforced per owner.

Staging temporarily duplicates bounded flow state; live acceptance uses only32
flow slots under the existing128 MiB cap. No claim is made that the largest
configuration fits that cap. Queue saturation can reject reload; it does not
create an unbounded control queue. A rejected update leaves the on-disk candidate
for the caller to restore, as with the original run command; the managed Agent defaults to single-engine mode and performs file restoration
itself; the new opt-in WorkerAgent uses the same restoration contract.

Native ASan/UBSan reload tests passed established-flow continuity, new-flow deny,
PAT/limit rejection, second-owner late rejection without partial publication,
and concurrent producers/maintenance/reload. Linux full regression and TSan passed workers-20261020-193420-60601; live
reload passed workers-live-20261020-195900-61662 after harness-only startup fixes. The live batch checks revision2 deny, retained UDP flows, absent new
flows, revision3 immutable-limit rejection and retained revision2 traffic.

Live reload result: generation2 acknowledged; retained UDP round trips before
and after incompatible generation3 rejection; new flows absent at egress in both
phases. TX120/failures0, sixty emissions per owner, current RSS2,142,208 bytes.
The test retains twenty mappings intentionally under30-second timeouts; idle
expiry was independently verified with3-second timeouts in the prior batch.

## Opt-in worker management API

`python3 control/agent.py --binary ENGINE --config CONFIG --state NEW_ROOT
--workers 2` selects WorkerAgent; omitting --workers retains the original engine.
`netforge check-workers CONFIG 1|2` validates the worker-specific subset and
sharding without raw sockets or interface changes, then stops its empty threads.
WorkerAgent uses this checker for initial and proposed files and invokes
run-workers for its owned runtime. Bearer authentication, loopback HTTP, request/
log bounds, expected-generation checking, acknowledged publication, rejected-file
restoration and uncertain-state blocking are inherited from Agent. Status adds
the fixed worker count; GET/metrics uses the real SIGUSR1 owner-safe snapshot.

Worker count cannot change during a deployment. Dynamic neighbors/global least
connections remain rejected. Runtime compatibility is authoritative: a candidate
can pass standalone shape validation but still be rejected for immutable running
limits; the prior file/state remain active. This adapter does not inherit
DurableAgent journaling, crash recovery or flow-state survival.

The new worker-api-batch.sh builds/tests serially in the existing512 MiB builder,
then uses one128 MiB/.5CPU/64PID network-none container with private veths and
NET_ADMIN/NET_RAW. It uses Docker's default seccomp (not unconfined) to support
the cached Python supervisor; the XDP-specific profile is not expanded. The real
HTTP test requires authentication, generation2 publication, old UDP continuity,
new-flow denial, incompatible-limit rejection/file restoration, unsupported
config and stale-publication rejection, TX120/failures0 metrics and owned shutdown.
No installs, old lab changes, deletions or host networking changes. The default
CMake suite now contains16 tests. Linux/live worker API acceptance is pending.

Native ASan/UBSan engine build and worker-adapter checks passed, including the
real worker checker,1/2 counts, ACK, metrics, rejection/restoration and unsupported
preflight. Original control, clean durable recovery/write-failure and health
controller tests also passed against the updated native engine. Linux pidfd
cases remain covered by prior Linux runs; this native run skipped those cases.
