#!/usr/bin/env python3
"""Read-only DPDK inventory and cached dependency simulation. Installs nothing."""
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess


def main():
    if platform.system() != "Linux":
        raise SystemExit("Run in the existing NetForge Linux builder")
    report = {"architecture": platform.machine(), "kernel": platform.release(),
              "tools": {name: shutil.which(name) for name in ("cc", "pkg-config", "meson", "ninja")},
              "allowed_cpus": sorted(os.sched_getaffinity(0)),
              "builder_free_disk_bytes": shutil.disk_usage("/tmp").free,
              "scope": "Read-only inventory and apt simulation; no downloads, installs, device binding, mounts or hugepage changes"}
    if shutil.which("pkg-config"):
        result = subprocess.run(["pkg-config", "--modversion", "libdpdk"],
                                capture_output=True, text=True, timeout=5)
        report["installed_dpdk_version"] = result.stdout.strip() if result.returncode == 0 else None
    info = pathlib.Path("/proc/meminfo").read_text()
    report["hugepage_inventory_only"] = [line for line in info.splitlines() if line.startswith(("HugePages_", "Hugepagesize:"))]
    if not shutil.which("apt-get") or not shutil.which("apt-cache"):
        report["dependency_plan"] = {"error": "apt tools unavailable"}
    else:
        simulation = subprocess.run(["apt-get", "--simulate", "--no-install-recommends", "install", "libdpdk-dev"],
                                    capture_output=True, text=True, timeout=25)
        selected = re.findall(r"^Inst (\S+)(?: \[[^]]+\])? \((\S+)", simulation.stdout, re.MULTILINE)
        plan = {"simulation_exit_code": simulation.returncode, "package_count": len(selected),
                "estimated_gross_installed_bytes": 0, "estimated_download_bytes": 0,
                "missing_size_metadata": [], "packages": [name for name, _ in selected]}
        if simulation.returncode:
            plan["error"] = (simulation.stderr or simulation.stdout)[-1600:]
        elif len(selected) > 256:
            plan["error"] = "over 256 packages; metadata expansion skipped"
        elif selected:
            metadata = subprocess.run(["apt-cache", "show", *[name + "=" + version for name, version in selected]],
                                      capture_output=True, text=True, timeout=15)
            records = {}
            for paragraph in metadata.stdout.split("\n\n"):
                fields = dict(line.split(": ", 1) for line in paragraph.splitlines()
                              if ": " in line and not line.startswith(" "))
                records[(fields.get("Package"), fields.get("Version"))] = fields
            for name, version in selected:
                fields = records.get((name.split(":")[0], version), {})
                if not fields.get("Installed-Size", "").isdigit() or not fields.get("Size", "").isdigit():
                    plan["missing_size_metadata"].append(name)
                else:
                    plan["estimated_gross_installed_bytes"] += int(fields["Installed-Size"]) * 1024
                    plan["estimated_download_bytes"] += int(fields["Size"])
        plan["limitations"] = "cached selected-version metadata; not peak disk usage or proof of runtime support"
        report["dependency_plan"] = plan
    report["next_design"] = "Bounded no-hugepage, single-worker virtual/PCAP path before any physical NIC experiment; runtime support remains unverified"
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
