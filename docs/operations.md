# Operations

Use [getting started](getting-started.md) for portable verification and the
[demo guide](demo.md) for presentation. The commands below describe Linux live
operation only after dedicated lab interfaces and policy have been prepared.

## Validate first

```sh
/path/to/netforge check /path/to/appliance.conf
```

`check` parses/validates without opening packet sockets. `run` additionally checks
that kernel IPv4 forwarding is disabled, configured devices exist with matching
MACs, and the kernel does not own appliance/service IPs. These checks do not
create interfaces or repair a wrong environment.

## Reference and worker processes

```sh
/path/to/netforge run /path/to/appliance.conf
/path/to/netforge check-workers /path/to/workers.conf 2
/path/to/netforge run-workers /path/to/workers.conf 2
```

Run only one compatible appliance per lab path. Worker mode rejects unsupported
configuration rather than silently adopting reference behavior. Start a supervised
process using [the control guide](../control/README.md) when API publication and
owned process shutdown are needed.

| Signal | Effect |
|---|---|
| SIGTERM/SIGINT | Bounded shutdown and final counters |
| SIGHUP | Validate and attempt complete configuration publication |
| SIGUSR1 | Emit a fresh metrics snapshot |

Keep a higher generation in every candidate. Accepted immutable-limit/interface
changes require restart instead of reload. Existing flows retain their old
snapshot. A known rejection preserves the active policy; a lost ACK is uncertain
and must be reconciled. Durable agents recover acknowledged configuration at a
higher revision, losing flow state. See [durability](reference/durability.md).

## Observe and diagnose

Core metrics count processing decisions; socket/PMD sends do not prove delivery.
Packet-socket drops identify pre-receive queue pressure; successful reset-on-read
samples accumulate in software. Backend diagnostics cover the first four current
reference service/backend pairs, and rule diagnostics the first16 rules. Missing
series are unavailable, not zero. Health observation and traffic counters differ.

Save a metrics snapshot and render it locally:

```sh
python3 tools/metrics_view.py /path/to/snapshot.txt /path/to/new-snapshot.html
```

Use [results](results.md) to understand measurement boundaries. Avoid comparing
sanitized and optimized builds or capped virtual-network rates with physical NICs.

## End a session

Stop the exact process/container you started. `tools/lab.py stop --name NAME`
stops only matching owned lab resources and does not delete them. No broad prune,
volume removal or unrelated cleanup is required. Preserve failure logs before
retrying. Existing output paths are generally exclusive: choose a new run path.
