# Reference metrics and measurement boundaries

The engine/AF_PACKET adapter exposes Prometheus text on SIGUSR1 and shutdown. The
agent GET /metrics requests a fresh snapshot from its owned child. No monitoring
server, dashboard stack, dependency or Mac setting is installed by this feature.
Counters reset on process restart; high-water values are process-lifetime gauges.

## Engine measurements

| Metric group | Meaning |
|---|---|
| received/emitted/dropped/deferred/consumed totals | Core processing dispositions; received includes explicit queue reprocessing |
| engine_received_bytes_total | Ethernet frame bytes presented to process, including malformed frames and queue reprocessing |
| engine_emitted_bytes_total | Bytes in emitted core decisions, not necessarily successful socket sends |
| reprocessed_total / reprocessed_bytes_total | Queued original frames re-entering validation/routing after resolution |
| udp_mappings / tcp_mappings | Current tracked mappings |
| flows_expired_total / flows_high_water | Reclaimed idle mappings and peak combined table occupancy |
| acl_rule_evaluations_total | Candidate rules examined for new/untracked traffic |
| acl_rule_matches_total | First-match rule decisions; established mappings bypass ACL re-evaluation |
| acl_default_decisions_total | Untracked packets reaching policy with no matching rule |
| route_lookups_total / route_misses_total | Actual pipeline route searches, including related ICMP restoration |
| neighbor gauges / queued_bytes_high_water | Current cache/target/queued-frame bytes and peak queued bytes |
| arp_requests_total / neighbor_timeouts_total | Maintenance ARP queries and queued packets discarded on resolution timeout |

Received = emitted + dropped + deferred + consumed for core invocations, not unique
wire packets. Timer-generated ARP queries do not add core receive/emitted counts.
The adapter's send counts include those queries. Route/ACL counters describe work,
not distinct clients. Related ICMP and queued retries can cause additional searches.
Flow expiry now advances on idle maintenance as well as packet processing. The live
adapter normally polls at most 250 ms between maintenance opportunities, but there
is no hard scheduling-latency guarantee under CPU starvation.

## Actual AF_PACKET socket measurements

Per configured port (at most 16), five fixed series are emitted:

- port_rx_packets_total / port_rx_bytes_total: accepted-size non-PACKET_OUTGOING
  frames passed from recvfrom to the engine, including unrelated inbound frames.
- port_tx_packets_total / port_tx_bytes_total: complete successful send calls.
- port_tx_failures_total: failed or partial frame sends.

Aggregate tx_sent_total / tx_failures_total remain available. rx_oversize_total
counts rejected oversized frames separately. Ethernet bytes exclude FCS/preamble/
inter-frame gap and cannot be used directly as physical-wire bandwidth. Socket
send success is not proof of delivery to an application or physical NIC. Packet-socket receive queue statistics are now sampled separately as described
below; there is still no end-to-end packet loss or latency measurement in these counters.

## Process resources

- process_cpu_seconds_total: user plus system CPU time, sampled with
  [getrusage(RUSAGE_SELF)](https://man7.org/linux/man-pages/man2/getrusage.2.html).
- process_peak_rss_bytes: Linux ru_maxrss converted from KiB to bytes. Resource
  history survives exec: when launched by the Python agent this peak can include
  the forked child's Python/pre-exec footprint. It is not a pure steady-state C++
  memory measurement. Use the resident gauge for a current observation.
- process_resident_bytes: resident pages from /proc/self/statm multiplied by the
  system page size; this is an approximate kernel snapshot, not container memory.
  See the [kernel proc documentation](https://www.kernel.org/doc/html/latest/filesystems/proc.html).
- process_uptime_seconds: monotonic elapsed time after packet sockets initialize.

These resources describe the dataplane PID (including the peak pre-exec caveat), not the running Python agent,
backends, Docker VM or macOS. Use environment/container observations for those.
Unavailable process measurements are omitted rather than reported as zero.

## Bounded labels and remaining work

Port IDs are limited to 32 bytes; quotes/backslashes/newlines are escaped in
rendered labels. IDs cannot change during reload. Series are per port, not per
flow, client address, request token or backend connection. Lifetime ACL counters remain aggregate. The first16 rules also expose
generation-scoped checks/hits as described below. New aggregate
metrics add no history buffers or per-packet log output. The existing agent's
bounded snapshot buffer and terminal oversize marker remain in place.

Current source has native accounting/formatting/idle-expiry tests. Linux adapter and live API metrics passed the retained observability and later
worker/XDP acceptance gates; see VERIFICATION.md.
Live dashboarding, latency histograms, kernel-drop reporting, controlled packet capture,
health/fleet audit correlation and a reproducible load report remain unfinished.


## Offline operational snapshot view

`python3 tools/metrics_view.py SNAPSHOT NEW.html` renders a saved single metrics
snapshot into a searchable HTML table. No service, network call, polling loop or
new dependency is used. Socket I/O, core state, worker state, kernel decisions
and process resources remain distinct groups. Missing values remain absent.

Input is capped at256 KiB/256 series, values must be finite/nonnegative, duplicate
series are rejected, labels are HTML-escaped and existing output is refused.
Use one shutdown/SIGUSR1/API snapshot, not concatenated repeated snapshots.
This view provides no rates, timestamps, loss estimates or inferred health.
The retained TCP runtime snapshot was rendered locally and offline safety tests
passed. Per-rule/backend health metrics and pre-receive kernel drops remain open.


## Bounded reference ACL diagnostics

`netforge_acl_rule_checks{generation="N",rule="I"}` and
`netforge_acl_rule_hits{generation="N",rule="I"}` identify zero-based configured
rule indices for the first16 rules. Checks count evaluation attempts; hits count
first matching decisions, including explicit deny. Established state bypasses
ACL evaluation and adds no hit. Successful reload resets these gauges, with a
new generation label; failed publication leaves them unchanged. They are not
process-lifetime totals. Aggregate lifetime ACL counters still cover all rules.

`netforge_acl_rule_series_omitted` reports configured rules beyond the export
limit. Storage is fixed at32 uint64 values per engine; export adds at most33
series, not per-flow labels or an unbounded revision history. Worker engines
retain these counters internally, but the worker runtime currently exports its
existing owner summaries rather than per-rule engine snapshots. XDP has its
separate decision counters; these reference diagnostics are not a cross-backend
per-rule API. Native sanitizer checks and full18-test Linux reproduction passed on2026-11-06.
A packet-driven live snapshot check is prepared in tcp-stream-batch.sh.


## Health controller round metrics

A successfully reconciled health round writes private metrics.prom beside
accepted.json: accepted configuration generation, whether this round published,
endpoint count, and per-endpoint observed TCP reachability/success/failure streaks.
At most64 parsed IPv4:port endpoints produce195 series. These are current-round
gauges, not cumulative probes or backend traffic counts. Manually drained state
and TCP reachability are distinct; probe_up does not imply traffic eligibility.
Rejected/uncertain publication produces no metrics snapshot. Unchanged rounds
reconfirm config identity before exporting. The offline metrics view groups
these separately as Backend health. No HTTP server or polling service is added.
Native checker/controller tests and real Linux metrics export passed on2026-11-06
in health-fault-20261106-121322-27458.


## Linux packet-socket queue provenance

Four fixed aggregate series are added, without per-flow labels:
packet_socket_packets_total, packet_socket_drops_total,
packet_socket_statistics_read_errors_total and
packet_socket_statistics_available_ports (all prefixed netforge_).
PACKET_STATISTICS resets kernel counters on read; the adapter accumulates deltas
on roughly1-second loop opportunities and each report. Scheduling starvation
can delay samples;32-bit kernel counters can wrap between sufficiently distant
reads. Process restart resets the aggregate. Packet/drop totals are omitted
until at least one successful sample; availability/errors indicate incomplete
sampling. These count kernel socket observations, which can include outgoing
frames discarded by the userspace adapter, and differ from accepted-size RX/core
decisions. They are not NIC-wide drops, policy denies or end-to-end delivery loss.
See https://man7.org/linux/man-pages/man7/packet.7.html for reset semantics.
Hosted forced-pressure acceptance is prepared, not yet passed.
