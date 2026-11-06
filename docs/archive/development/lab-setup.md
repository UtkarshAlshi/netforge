# Isolated lab provisioning

The tool requires an existing local Linux image containing iproute2, ethtool and
sleep (the prepared netforge-lab:m0 image satisfies this). It neither downloads
nor builds the image. A completely fresh machine/image bootstrap remains open.
Existing acceptance resources are never adopted or recreated.

Read-only first step:

```sh
python3 tools/lab.py plan --name netforge-repro --subnet 10.79.0.0/24
```

After reviewing the plan, explicit creation:

```sh
python3 tools/lab.py up --name netforge-repro --subnet 10.79.0.0/24 --output /absolute/path/new-lab-directory
```

Creates only an internal Docker bridge and three uniquely named containers. Each
has128 MiB/no swap,.5CPU,64PIDs, no-new-privileges, dropped capabilities except
NET_RAW (and NET_ADMIN for endpoints), no mounts or published ports. All carry
io.netforge.lab ownership labels. The application container has forwarding0 and
no NET_ADMIN. Routes/offload changes affect only the newly created endpoints.
Output contains an observed-MAC UDP service config; no dataplane starts.

VM checks require Linux, at most2 CPUs/~2 GiB. Creation also requires10 GiB disk
headroom. Name/subnet collisions refuse before creation; stopped containers count
as collisions. The image ID is pinned after preflight. Partial creation is retained
for inspection; no rollback removes resources. Concurrent provisioning is not
supported; a Docker collision/error stops the command with partial resources
retained. This tool has no delete/prune action.

Status and stopping validate ownership of all three named containers:

```sh
python3 tools/lab.py status --name netforge-repro
python3 tools/lab.py stop --name netforge-repro
```

Stop is explicit and affects only that owned lab. It does not delete resources.
Re-running up after stop refuses collision; restarting/reconfiguring this new lab
is not automated yet. Do not use these names for the existing netforge-m0 lab.

Local plan tests and actual Docker plan/up passed on2026-11-06 for
netforge-repro. New-lab live UDP traffic acceptance passed repro-acceptance-batch.sh on
2026-11-06: direct SNAT, round-robin, affinity and TX11/failures0.
