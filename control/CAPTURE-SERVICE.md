# Bounded DPDK capture control service

This optional Linux pidfd-based supervisor manages finite PCAP jobs. It is not
a continuous NIC dataplane or a replacement for the stateful reference agent.
One process owns one fixed configuration/input snapshot; only supported
lan1/wan2 stateless policies pass compiler and runtime --check before jobs start.

```sh
python3 control/capture_agent.py --binary /path/nf-dpdk-ring --compiler /path/nf-xdp-compile --config /path/input.conf --input /path/input.pcap --state /path/new-private-state --port 9081
```

HTTP binds127.0.0.1 only. Read bearer credentials from state/token locally; never
put credentials in committed config, URLs or reports. GET /v1/status returns
fixed config/input SHA256, generation and bounded job outcomes. POST /v1/jobs
requires JSON expected_generation and request_id (1..64 ASCII alnum/_.-). Same
request_id returns the original job; it never starts a duplicate after response
loss. A different request is refused while a job runs or its watcher is finishing.
POST /v1/stop requires JSON id and can signal only the current owned pidfd.
No endpoint accepts arbitrary paths, executable names, configuration bodies or PIDs.

Bounds: one active child, four jobs/session,1 MiB fixed PCAP input,64 KiB config,
64 KiB child log,30-second child deadline,128-byte request body,2-second socket
read deadline. Output captures are exclusively created per job. Job summaries
include validated capture counts; partial/stopped/failed jobs are not reported
as complete. Logs contain no bearer credentials. Session output is bounded by
the fixed input plus four capture/log results.

SIGTERM/SIGINT closes the server and signals only its child via pidfd, escalating
after a bounded wait. Unknown jobs are refused. There is no durable restart
recovery: restarting with the same state directory is refused; a crashed
supervisor may leave a child until its finite job completes. Private state is
retained, never automatically deleted. No policy hot reload, per-flow state,
remote transport or physical NIC delivery is implied.

Lifecycle tests use controlled children; separate hosted acceptance exercises
authenticated HTTP against real DPDK and checks exact multi-route capture counts.
Both passed hosted Linux acceptance in run42222608259. See VERIFICATION.md.
