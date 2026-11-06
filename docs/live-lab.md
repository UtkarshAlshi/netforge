# Optional isolated Linux lab

**For a presentation, use the portable demo first.** It is complete without a
Docker image or special capabilities. Live lab setup is separate and optional.

## Reproducible hosted acceptance

The repository workflow is the maintained fresh-environment live acceptance path:
[Actions → build-and-test](https://github.com/UtkarshAlshi/netforge/actions/workflows/ci.yml).
On a fork you control, select **Run workflow**. It performs native checks and
creates an ephemeral Ubuntu24.04 Docker container capped at512MiB/no swap,
0.5CPU and128PIDs. It creates private veths only inside that container, exercises
real reference/worker/DPDK traffic and faults, and uploads sanitized reports.
It does not mount a host directory/NIC/Docker socket, bind a public service port,
use hugepages or change your Mac. Dependency installation happens on the hosted
runner's disposable container. Inspect `.github/workflows/ci.yml` and
`tools/hosted_dpdk.sh` for the exact commands. Artifacts expire after three days;
small final-release raw observations are permanently committed in `results/`.

## Existing local Docker lab helper

`tools/lab.py` supports an already-prepared local Linux image with `iproute2`,
`ethtool`, `ping` and `sleep`, default tag `netforge-lab:m0`. It never pulls/builds
an image itself. It requires a Linux Docker engine capped at no more than2CPUs
and about2GiB RAM, plus10GiB free host disk. If you do not already have that image,
use hosted acceptance rather than assuming the helper is a fresh image installer.

Read-only inspection/planning:

```sh
docker info --format 'CPUs={{.NCPU}} Memory={{.MemTotal}}'
python3 tools/lab.py plan --name netforge-repro --subnet 10.79.0.0/24
```

After reviewing its explicit plan, optional creation:

```sh
python3 tools/lab.py up --name netforge-repro --subnet 10.79.0.0/24 --output ../netforge-live-lab
```

This creates an internal bridge and three owned, uniquely named containers:
client A, backend B and appliance. Each has128MiB/no swap,0.5CPU,64PIDs,
no-new-privileges, no host mounts and no published ports. Endpoint NET_ADMIN
changes routes/offload only inside those new containers; the appliance has NET_RAW.
The helper creates an observed-MAC configuration but does not start a dataplane.
Name/subnet collisions refuse; partial resources are retained for inspection.

```sh
python3 tools/lab.py status --name netforge-repro
python3 tools/lab.py stop --name netforge-repro
```

`stop` leaves resources intact. There is no delete/prune operation. This helper is
for the documented small existing-image lab, not an arbitrary production network.
The older scripts under `lab/` are preserved learning material and require their
own Linux namespace assumptions; do not run them blindly on a workstation.

For policy/runtime details see [operations](operations.md), [configuration](reference/configuration.md),
[control](../control/README.md), [XDP](../backends/xdp/README.md) and [DPDK](../backends/dpdk/README.md).
