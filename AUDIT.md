# Bounded publication audit

Authenticated GET /v1/audit returns the last64 authenticated POST /v1/config
outcomes for this supervisor instance. Each event has a monotonic local sequence,
wall-clock timestamp_ns, request_id, bounded expected_generation, candidate_sha256
when the body fits the config bound, applied_generation, attempted http_status,
uncertain flag and applied/rejected/uncertain/unavailable outcome. It stores no
bearer tokens, configuration bodies or arbitrary error strings. Snapshot fields
include capacity64, last_sequence, evicted and persistent:false.

Clients may send X-Netforge-Request-ID with1..64 ASCII letters/digits or -_.;
otherwise a random ID is generated. Invalid IDs receive400 and are never reflected.
Publication responses include request_id and an X-Netforge-Request-ID header.
IDs are correlation labels, not idempotency keys; repeated IDs get distinct local
sequence numbers and do not bypass expected-generation checking. Unauthorized
requests cannot read the audit or consume its publication-event capacity.

The outcome is recorded before response I/O. A lost client connection after an
acknowledged publication does not become a second failure event and does not
undo applied state. http_status names the attempted response, not proof that the
client received it. Candidate hash is an identity reference, not proof of validity;
rejected candidates can have hashes too. Sequence orders events if the wall clock
moves. Metrics, audit and status are individually sampled, not a global transaction.

The64-event ring is bounded in memory and resets on supervisor restart, including
DurableAgent restart. It is not a durable/tamper-proof security log. A trusted
same-UID operator can alter process/files. No packet payload or flow tuple audit
is added. There is no remote exporter or dashboard in this change.

In-memory handler checks passed hash/privacy/bounds/eviction and one applied event
after a controlled response disconnect. Native original control, clean durability
and worker-adapter regression are checked separately. Real HTTP audit tests and actual worker-engine hash/generation correlation
passed audit-batch.sh on 2026-11-06 (audit-20261106-104415-15619). That
batch reuses the verified worker API runtime/Python instead of compiling C++ or
installing dependencies. It retains old evidence and uses a fresh capped private
container, then stops test-owned processes/container without deletion.
