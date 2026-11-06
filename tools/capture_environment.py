#!/usr/bin/env python3
"""User-run read-only NetForge environment capture; no installs or configuration changes."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import platform
import subprocess

CONTAINERS = ["netforge-m0-a", "netforge-m0-b", "netforge-m0-r", "netforge-live", "netforge-build"]


def command(args, timeout=12):
    result = subprocess.run(args, capture_output=True, text=True, timeout=timeout, check=False)
    if result.returncode:
        raise RuntimeError("command failed: " + " ".join(args[:3]) + ": " + result.stderr.strip()[:400])
    return result.stdout.strip()


def capture(project, artifacts):
    issues = []
    def optional(args, timeout=12):
        try:
            return command(args, timeout)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            issues.append(str(error)[:500])
            return None
    now = datetime.datetime.now(datetime.timezone.utc)
    disk = os.statvfs(project)
    host = {"os": platform.system(), "architecture": platform.machine(),
            "python": platform.python_version(), "free_disk_bytes": disk.f_bavail * disk.f_frsize}
    if platform.system() == "Darwin":
        host["macos_version"] = optional(["sw_vers", "-productVersion"])
        ram = optional(["sysctl", "-n", "hw.memsize"])
        host["physical_ram_bytes"] = int(ram) if ram and ram.isdigit() else None
    daemon_raw = optional(["docker", "info", "--format", "{{json .}}"])
    daemon = None
    if daemon_raw:
        info = json.loads(daemon_raw)
        daemon = {key: info.get(key) for key in ["ServerVersion", "OSType", "Architecture", "NCPU", "MemTotal", "Driver", "CgroupVersion"]}
        if info.get("NCPU", 0) > 2 or info.get("MemTotal", 0) > 2300000000:
            issues.append("Docker VM exceeds the agreed 2-CPU/about-2-GiB ceiling; no setting was changed")
    containers = []
    for name in CONTAINERS:
        raw = optional(["docker", "inspect", name])
        if not raw:
            continue
        item = json.loads(raw)[0]
        limits = item["HostConfig"]
        record = {"name": name, "state": item["State"]["Status"], "image_id": item.get("Image"),
                  "limits": {key: limits.get(key) for key in ["Memory", "MemorySwap", "NanoCpus", "PidsLimit", "CapAdd", "CapDrop", "Privileged", "NetworkMode", "SecurityOpt"]},
                  "networks": {key: {field: value.get(field) for field in ["IPAddress", "IPPrefixLen", "MacAddress"]}
                               for key, value in item["NetworkSettings"]["Networks"].items()}}
        expected_memory = (512 if name == "netforge-build" else 128) * 1024 * 1024
        if limits.get("Memory") != expected_memory or limits.get("MemorySwap") != expected_memory or limits.get("NanoCpus") != 500000000 or limits.get("PidsLimit") != (128 if name == "netforge-build" else 64):
            issues.append(name + " resource limits differ from the agreed lab profile; no setting was changed")
        if limits.get("Privileged"):
            issues.append(name + " is privileged; no setting was changed")
        if item["State"]["Status"] == "running":
            record["interfaces"] = optional(["docker", "exec", name, "ip", "-br", "addr"])
            record["routes"] = optional(["docker", "exec", name, "ip", "route"])
            record["kernel_forwarding"] = optional(["docker", "exec", name, "cat", "/proc/sys/net/ipv4/ip_forward"])
            if name in {"netforge-m0-a", "netforge-m0-b"}:
                record["offloads"] = optional(["docker", "exec", name, "ethtool", "-k", "eth0"])
            if name == "netforge-build":
                record["compiler"] = optional(["docker", "exec", name, "g++", "-dumpfullversion"])
                record["python"] = optional(["docker", "exec", name, "python3", "--version"])
        containers.append(record)
    network_raw = optional(["docker", "network", "inspect", "netforge-m0-net"])
    network = None
    if network_raw:
        item = json.loads(network_raw)[0]
        network = {key: item.get(key) for key in ["Name", "Driver", "Internal", "IPAM"]}
        if item.get("Internal") is not True:
            issues.append("netforge-m0-net is not internal; no setting was changed")
    stats_raw = optional(["docker", "stats", "--no-stream", "--format", "{{json .}}", *CONTAINERS], timeout=20)
    stats = [json.loads(line) for line in stats_raw.splitlines() if line.strip()] if stats_raw else []
    hashes = {}
    for path in sorted(artifacts.glob("*-linux-aarch64")):
        if path.is_file():
            hasher = hashlib.sha256()
            with path.open("rb") as source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    hasher.update(block)
            hashes[path.name] = {"bytes": path.stat().st_size, "sha256": hasher.hexdigest()}
    manifest = project / "SOURCE-MANIFEST.sha256"
    source_hash = hashlib.sha256(manifest.read_bytes()).hexdigest() if manifest.is_file() else None
    if host["free_disk_bytes"] < 10 * 1024 ** 3:
        issues.append("Less than 10 GiB free: defer additional dependency/build expansion")
    return {"schema": 1, "captured_utc": now.isoformat(), "host": host, "docker": daemon,
            "containers": containers, "lab_network": network, "container_stats": stats,
            "local_binary_artifacts": hashes, "source_manifest_sha256": source_hash,
            "issues": issues, "measurement_limits": ["Docker/veth environment; no physical-NIC performance inference",
                "source manifest fingerprint does not prove that a particular binary was built from that source",
                "runtime file copies and process observations are not a reproducible clean setup recipe"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="new JSON file; refuses to overwrite")
    args = parser.parse_args()
    project = pathlib.Path(__file__).resolve().parent.parent
    artifacts = project.parent / "netforge-live"
    output = pathlib.Path(args.output)
    if output.exists():
        raise SystemExit("Output already exists; choose a new path")
    result = capture(project, artifacts)
    with output.open("x") as target:
        json.dump(result, target, indent=2)
        target.write("\n")
    print("SAVED " + str(output.resolve()))
    print(json.dumps({"free_disk_GiB": round(result["host"]["free_disk_bytes"] / 1024 ** 3, 2),
                      "docker": result["docker"], "issues": result["issues"]}, indent=2))


if __name__ == "__main__":
    main()
