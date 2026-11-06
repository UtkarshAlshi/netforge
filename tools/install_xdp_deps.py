#!/usr/bin/env python3
"""Size-guarded XDP build dependencies, only inside the designated lab builder."""
import json
import os
import platform
import re
import shutil
import subprocess
import tempfile

PACKAGES = ["clang-18", "libbpf-dev", "pkg-config"]


def main():
    if platform.system() != "Linux" or os.environ.get("NETFORGE_XDP_BUILD_CONTAINER") != "1":
        raise SystemExit("Run only through the NetForge XDP batch in the existing builder")
    environment = {**os.environ, "DEBIAN_FRONTEND": "noninteractive"}
    plan = subprocess.run(["apt-get", "--simulate", "--no-install-recommends", "install", *PACKAGES],
                          capture_output=True, text=True, check=True, timeout=30)
    planned = re.findall(r"^Inst (\S+)(?: \[[^]]+\])? \((\S+)", plan.stdout, re.MULTILINE)
    if len(planned) > 100:
        raise SystemExit("Refusing dependency plan over 100 packages")
    installed, download = 0, 0
    for name, version in planned:
        metadata = subprocess.run(["apt-cache", "show", name], capture_output=True, text=True, check=True, timeout=10).stdout
        selected = None
        for paragraph in metadata.split("\n\n"):
            fields = dict(line.split(": ", 1) for line in paragraph.splitlines() if ": " in line and not line.startswith(" "))
            if fields.get("Version") == version:
                selected = fields; break
        if not selected or not selected.get("Installed-Size", "").isdigit() or not selected.get("Size", "").isdigit():
            raise SystemExit("Cannot conservatively size package " + name + " " + version)
        installed += int(selected["Installed-Size"]) * 1024
        download += int(selected["Size"])
    print(json.dumps({"packages": len(planned), "estimated_gross_installed_MiB": round(installed / 1048576, 1),
                      "estimated_download_MiB": round(download / 1048576, 1)}, indent=2), flush=True)
    if installed > 768 * 1048576 or download > 256 * 1048576:
        raise SystemExit("Dependency-size ceiling exceeded; no installation attempted")
    if shutil.disk_usage("/tmp").free < installed + download + 512 * 1048576:
        raise SystemExit("Insufficient builder disk headroom; no installation attempted")
    with tempfile.NamedTemporaryFile(mode="w+", prefix="netforge-xdp-deps-", suffix=".log", dir="/tmp", delete=False) as log:
        print("Installing in builder; retained log: " + log.name, flush=True)
        result = subprocess.run(["apt-get", "-y", "--no-install-recommends", "-o", "Acquire::Retries=1",
                                 "-o", "Acquire::http::Timeout=20", "-o", "Acquire::https::Timeout=20",
                                 "-o", "Dpkg::Use-Pty=0", "install", *PACKAGES], env=environment, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            log.flush(); log.seek(0); text = log.read()
            print(text[-4000:], flush=True)
            raise SystemExit("Dependency installation failed; full builder log retained")
    print("PASS: bounded XDP toolchain installed in existing build container only", flush=True)


if __name__ == "__main__":
    main()
