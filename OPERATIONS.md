# Operations

## Local verification

Use offline check/replay/tests on macOS without Docker. Do not run live mode there.
Keep builds serial and benchmark repetitions bounded. Existing workstation budget:
Docker VM 2 CPUs/~2 GiB; each NetForge lab container 128 MiB RAM/no container swap,
0.5 CPU and 64 PIDs. Targeted source/build edits do not change these limits.

## Linux live prerequisites (acceptance pending)

`netforge run CONFIG` binds only configured interface devices. It refuses kernel
forwarding enabled in its namespace, kernel ownership of any configured port IP or
service VIP, missing interfaces, and MAC mismatch. Prepare dedicated unnumbered
interfaces in the lab, with kernel forwarding disabled and static neighbor entries
in CONFIG for the test endpoints. The application answers ARP for its own IP/VIPs.
Check configured MTUs against actual interfaces. Keep packet offloads in the
experimental manifest: veth/NIC checksum offloads can expose incomplete transport
checksums to AF_PACKET, which this strict validator will reject. Any offload change
must be restricted to a lab interface, never a Mac or unrelated interface.

The adapter does not create namespaces/interfaces, modify addresses/routes or
sysctls, or alter host firewall rules. It ignores PACKET_OUTGOING frames to avoid
recapturing its own transmission. It sends frames through the configured bound
socket; failed/partial sends are counted and discarded without retries.

## Signals

- SIGINT/SIGTERM: stop after the current bounded iteration, print counters, close
  sockets through RAII. No persistent resources are created by the process.
- SIGHUP: reread the same config path. Publish a strictly newer valid generation,
  or log config_rejected and retain the previous generation.
- SIGUSR1: emit Prometheus-format counters to stdout without terminating.

Startup/publication/rejection/shutdown events are JSON lines on stderr. Per-packet
logging is avoided in live mode. Unexpected adapter errors exit nonzero; a process
supervisor may restart it, but all UDP mappings are lost. Config files should be
prepared completely before replacement; no network management API is exposed.

## Known Docker lab caveats

The current Linux reference lab uses two /25 IP subnets on one internal /24 Docker
bridge. It is one broadcast domain, not two physical links. The kernel router
still has redirects enabled. Previous pings were kernel forwarding, not C++ output.
Do not recreate or delete these containers merely to run offline tests. Manual
Docker execution is required: one small block, purpose/expected effect explained,
then wait for output. Ask before deleting containers/images/volumes/data.

Docker image-inventory `blob not found` remains unresolved; no broad prune/reset is
part of this project. A kernel router and the C++ process must never concurrently
own the same appliance addresses during live acceptance.

## End of a lab session

Stop only the three named NetForge containers when no test is in progress; quit
Docker Desktop afterward if desired. Container restart may restore Docker-managed
addresses and erase manually added routes. Inspect actual state before resuming.

## Read-only environment manifest

Run `python3 tools/capture_environment.py NEW_OUTPUT_JSON` from the operator's
machine using the agreed manual Docker workflow. It inspects only the five known
NetForge containers and the named lab network, records route/interface/offload
state, compiler/runtime versions, existing caps/limits, one-shot container stats,
Mac disk/RAM facts and hashes of local Linux artifacts. It writes one new report
and refuses to overwrite an existing file. No shell configuration, Docker settings,
packages, image inventory, namespace, interface, route or sysctl is modified.
The report flags drift/headroom issues but does not repair them automatically.
Source-manifest fingerprints and binary hashes are separate observations; they do
not establish build provenance or a clean reproducible deployment by themselves.
