#!/usr/bin/env python3
"""Read-only Linux XDP/toolchain inventory and simulated dependency sizing."""
import argparse
import json
import pathlib
import platform
import re
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    if platform.system() != "Linux":
        raise SystemExit("Run inside the existing Linux build container")
    deadline = time.monotonic() + 45
    def command(arguments):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("preflight time budget exhausted")
        result = subprocess.run(arguments, capture_output=True, text=True, timeout=min(10, remaining), check=False)
        return result.returncode, result.stdout, result.stderr
    tools = {name: shutil.which(name) for name in ["clang", "clang-18", "bpftool", "pkg-config", "cc", "readelf"]}
    caps = {}
    for line in pathlib.Path("/proc/self/status").read_text().splitlines():
        if line.startswith(("CapEff:", "CapBnd:", "NoNewPrivs:", "Seccomp:")):
            key, value = line.split(":", 1)
            caps[key] = value.strip()
    effective = int(caps.get("CapEff", "0"), 16)
    cap_names = {"NET_ADMIN": 12, "NET_RAW": 13, "SYS_ADMIN": 21, "PERFMON": 38, "BPF": 39}
    report = {"kernel": platform.release(), "architecture": platform.machine(), "tools": tools,
              "process_security": caps, "effective_capabilities": {key: bool(effective & (1 << bit)) for key, bit in cap_names.items()},
              "kernel_btf_available": pathlib.Path("/sys/kernel/btf/vmlinux").is_file(),
              "notes": ["This script creates/loads/attaches no BPF program or map", "Build-container capabilities do not prove kernel or future lab support",
                        "No install, download, apt update, mounts, interface or resource-setting changes"]}
    config = pathlib.Path("/proc/config.gz")
    if config.is_file():
        import gzip
        with gzip.open(config, "rt") as source:
            text = source.read(1048576)
        report["kernel_options"] = [line for line in text.splitlines() if re.match(r"CONFIG_(BPF|BPF_SYSCALL|BPF_JIT|NET_CLS_BPF|NET_ACT_BPF|XDP_SOCKETS)=", line)]
    else:
        report["kernel_options"] = "not exposed; actual isolated load test still required"
    if shutil.which("apt-get") and shutil.which("apt-cache"):
        code, output, error = command(["apt-get", "--simulate", "--no-install-recommends", "install",
                                       "clang-18", "libbpf-dev", "bpftool", "pkg-config"])
        packages = list(dict.fromkeys(re.findall(r"^Inst ([^\s]+)", output, flags=re.MULTILINE)))
        estimate = {"simulation_exit_code": code, "packages": packages, "package_count": len(packages),
                    "estimated_gross_installed_bytes": 0, "estimated_download_bytes": 0, "missing_size_metadata": []}
        if len(packages) > 100:
            estimate["note"] = "Large plan: metadata sizing skipped"
        else:
            for package in packages:
                status, metadata, _ = command(["apt-cache", "show", package])
                installed = re.search(r"^Installed-Size: (\d+)$", metadata, re.MULTILINE)
                download = re.search(r"^Size: (\d+)$", metadata, re.MULTILINE)
                if status == 0 and installed and download:
                    estimate["estimated_gross_installed_bytes"] += int(installed[1]) * 1024
                    estimate["estimated_download_bytes"] += int(download[1])
                else:
                    estimate["missing_size_metadata"].append(package)
        if code:
            estimate["simulation_error"] = (error or output)[-1200:]
        estimate["limitations"] = "cached candidate sizes only; not a guaranteed peak disk budget or a download authorization"
        report["dependency_plan"] = estimate
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
