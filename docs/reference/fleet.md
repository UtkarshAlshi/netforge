# Journaled sequential fleet publication

`python3 control/fleet_journal.py MANIFEST PRIVATE_STATE_DIRECTORY` uses the same
bounded loopback manifest as fleet.py, including per-agent tokens, exact expected
applied generations and candidate config files. This opt-in controller adds a
private, locked, atomic/fsynced journal. The original fleet.py remains available.

The journal retains only endpoint/name, expected and desired generations, config
SHA-256 and pending/intended/verified states. It contains no bearer tokens or
configuration bodies. Keep the manifest and candidate files to resume. The plan
must match exactly; a changed plan requires a new dedicated state directory.
Same-UID tampering is outside the trust boundary; there is no signed audit log.

All agents undergo read-only preflight before any publication. A matching target
requires both exact generation and exact configuration hash. Otherwise pending
nodes must match their expected generation and have durable_high_water below the
candidate revision. Intent is fsynced before POST. Exact acknowledgement plus
GET/config validation is required before marking verified. Successful earlier
nodes remain applied when a later node fails: this is not a global transaction.

On rerun, an exact acknowledged target is reconciled without another POST. An
interrupted intent that does not match the target fails closed: no blind retry,
rollback or revision reuse. Investigate the agent and prepare an explicit new
plan at higher revisions, using its current applied generation as expected and
advancing beyond durable_high_water. External drift of a verified node also
stops reconciliation. Concurrent publishers can race after preflight; per-agent
expected-generation checks remain authoritative. Controller disk failure stops
progress; agent state must be inspected on recovery.

At most 16 nodes and 64 KiB per config/manifest are admitted by load_plan; the
journal read is bounded to 32 KiB. Loopback HTTP must be locally trusted or carried
by an operator-managed secure tunnel; no remote TLS/membership/consensus service
is implemented. This does not preserve connection state across process crashes.

Local transport-double tests passed locking, changed-plan refusal, response-loss
reconciliation, no duplicate publication and drift refusal. fleet-batch.sh passed Linux acceptance on 2026-10-20: two logical durable agents in the existing capped live
container, actual C++ children and authenticated HTTP; stale preflight, partial
runtime rejection, corrected plan and injected lost-response reconciliation.
They share a machine/network namespace and are not independent failure domains.
Test state is retained; test-owned processes stop. No images, existing containers,
host interfaces or resource settings are deleted or changed.

Evidence: netforge-live/fleet-20261020-191906-59715. Both actual children
acknowledged generation 9 and reported zero TX failures; all test processes stopped.


Connection refusal after publication intent is journaled is treated conservatively:
the controller does not infer that a request was never applied from a transport
exception. On endpoint rejoin, exact target generation/hash reconciles without a
POST; otherwise an intended revision requires an explicit higher-revision
corrective plan using the observed current revisions. Availability recovery alone
does not authorize replay. A new deterministic test covers refusal before apply.
