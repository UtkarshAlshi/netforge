#!/usr/bin/env python3
"""Guarded builder-only ring-PMD runtime install plus extracted SDK headers."""
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys

PACKAGES = ["librte-net-ring24", "librte-mempool-ring24", "libbsd-dev"]


def fields(text):
    return dict(line.split(": ", 1) for line in text.splitlines() if ": " in line and not line.startswith(" "))


def main():
    if platform.system() != "Linux" or os.environ.get("NETFORGE_DPDK_BUILD_CONTAINER") != "1":
        raise SystemExit("Use only in the existing capped NetForge builder")
    if os.environ.get("NETFORGE_DPDK_AF_PACKET") == "1":
        PACKAGES.append("librte-net-af-packet24")
    sdk = pathlib.Path(sys.argv[1])
    sdk.mkdir(parents=True, exist_ok=False)
    result = subprocess.run(["apt-get", "--simulate", "--no-install-recommends", "install", *PACKAGES],
                            capture_output=True, text=True, timeout=25, check=True)
    if re.search(r"^Inst \S+ \[", result.stdout, re.MULTILINE):
        raise SystemExit("Refusing upgrades of existing builder packages")
    planned = re.findall(r"^Inst (\S+)(?: \[[^]]+\])? \((\S+)", result.stdout, re.MULTILINE)
    if len(planned) > 64 or any(name.split(":")[0] in {"systemd", "udev", "rdma-core"} for name, _ in planned):
        raise SystemExit("Dependency plan exceeds restricted ring-PMD scope")
    metadata = subprocess.run(["apt-cache", "show", "--no-all-versions", "libdpdk-dev"],
                              capture_output=True, text=True, timeout=10, check=True).stdout
    header = fields(metadata.split("\n\n")[0])
    selected = planned + [("libdpdk-dev", header["Version"])]
    raw = subprocess.run(["apt-cache", "show", *[name + "=" + version for name, version in selected]],
                         capture_output=True, text=True, timeout=15, check=True).stdout
    records = {}
    for paragraph in raw.split("\n\n"):
        item = fields(paragraph); records[(item.get("Package"), item.get("Version"))] = item
    installed = download = 0
    for name, version in selected:
        item = records.get((name.split(":")[0], version), {})
        if not item.get("Size", "").isdigit() or not item.get("Installed-Size", "").isdigit():
            raise SystemExit("Missing selected-version size metadata: " + name)
        installed += int(item["Installed-Size"]) * 1024
        download += int(item["Size"])
    print(json.dumps({"runtime_packages": len(planned), "sdk": "extract only; no full dev-package install",
                      "estimated_gross_installed_MiB": round(installed / 1048576, 2),
                      "estimated_download_MiB": round(download / 1048576, 2)}, indent=2), flush=True)
    if installed > 256 * 1048576 or download > 128 * 1048576:
        raise SystemExit("DPDK subset dependency budget exceeded; nothing installed")
    if shutil.disk_usage("/tmp").free < installed + download + 512 * 1048576:
        raise SystemExit("Insufficient builder disk headroom")
    log = sdk / "install.log"
    with log.open("w") as stream:
        arguments = ["apt-get", "-y", "--no-install-recommends", "-o", "Acquire::Retries=1",
                     "-o", "Acquire::http::Timeout=20", "-o", "Acquire::https::Timeout=20", *["install", *PACKAGES]]
        subprocess.run(arguments, env={**os.environ, "DEBIAN_FRONTEND": "noninteractive"},
                       stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=240)
        subprocess.run(["apt-get", "-o", "Acquire::Retries=1", "-o", "Acquire::http::Timeout=20",
                        "-o", "Acquire::https::Timeout=20", "download", "libdpdk-dev=" + header["Version"]],
                       cwd=sdk, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=120)
    archives = list(sdk.glob("libdpdk-dev_*.deb"))
    if len(archives) != 1: raise SystemExit("Expected exactly one SDK archive")
    subprocess.run(["dpkg-deb", "--extract", str(archives[0]), str(sdk / "headers")], check=True, timeout=30)
    print("PASS: restricted DPDK runtime installed; SDK extracted; retained log=" + str(log), flush=True)


if __name__ == "__main__":
    main()
