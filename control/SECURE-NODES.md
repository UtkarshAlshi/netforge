# Explicit secure nodes and membership

`secure_node.py` opts a reference appliance into TLS1.3 mutual certificate
verification plus the existing bearer authorization. It defaults to loopback;
remote binding requires an explicit `--bind ADDRESS`. No plaintext fallback,
port publication, firewall change, discovery or certificate generation occurs.
Provide `--binary`, `--config`, a new private `--state`, `--node-id`,
`--certificate`, private owned `--key`, and client-trust `--ca`. Provision these
through an existing trusted channel. Private keys must be owned by the current
UID with no group/other permissions. The API token is created in the state dir.
Use separate service-specific CA trust and certificates with server/client EKUs
and host/IP SANs. Every client trusted by that CA still needs the bearer token.
TLS handshake and request deadlines are two seconds; the single HTTP server
bounds concurrency. A slow authenticated client can delay others until timeout.

`secure_fleet.py observe members.json` reads an explicit list of1..16 unique
members. Each object contains exactly `name`, `node_id`, `url` (HTTPS host and
explicit port), `ca`, `certificate`, `key`, `token_file`, `config`, and
`expected_generation`. File paths resolve against the manifest. The client
verifies CA and hostname; node ID in authenticated responses must also match.
Keep the manifest and credential paths in an operator-owned directory. No
untrusted manifest is accepted through an API. Observations use at most four
parallel connections, an eight-second request timeout and bounded response reads.
They report reachability/process state, not peer consensus or dataplane health.

`secure_fleet.py rollout members.json --journal NEW_PRIVATE_DIRECTORY` uses the
existing full-fleet preflight and durable intent journal. Transport identity
binds the node ID, CA and client certificate hashes into the exact plan; tokens
and private keys are excluded. Reconciliation never blindly repeats an unknown
publication. An operator supplies a new higher-revision corrective plan after
an unresolved intent. Certificate rotation requires a new plan/journal.

This is explicit static membership and secure management transport, not Raft,
automatic membership, packet-state replication or automatic failover. The secure
node wrapper currently uses the reference process supervisor, with cold process
startup and no durable remote-wrapper recovery. Durable loopback reference and
worker agents remain separate tools. Tests exercise real TLS sockets and journal publication against a controlled
engine double on Linux/macOS, plus an actual reference dataplane on private
hosted veths. The latter verifies TCP service traffic and backend counters. Independent-machine failure domains and deployed
certificate operations still require a separately provisioned Linux environment.
