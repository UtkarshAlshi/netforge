#!/usr/bin/env python3
"""Build the real ring-PMD acceptance binary with the extracted matching SDK."""
import os
import pathlib
import subprocess
import sys

source, sdk, output = map(pathlib.Path, sys.argv[1:])
triplet = subprocess.check_output(["gcc", "-dumpmachine"], text=True).strip()
packages = {"librte-net-ring24": "librte_net_ring.so.24",
            "librte-mempool-ring24": "librte_mempool_ring.so.24",
            "librte-ethdev24": "librte_ethdev.so.24", "librte-mbuf24": "librte_mbuf.so.24",
            "librte-mempool24": "librte_mempool.so.24", "librte-ring24": "librte_ring.so.24",
            "librte-eal24": "librte_eal.so.24"}
libraries = []
for package, name in packages.items():
    paths = subprocess.check_output(["dpkg-query", "-L", package], text=True).splitlines()
    matches = [path for path in paths if pathlib.Path(path).name == name and pathlib.Path(path).is_file()]
    if not matches: raise SystemExit("Missing installed runtime library " + name)
    libraries.append(matches[0])
archive = os.environ.get("NETFORGE_DPDK_CORE_ARCHIVE")
if archive:
    core = pathlib.Path(archive)
    if not core.is_file() or core.name != "libnetforge_core.a":
        raise SystemExit("Expected verified CMake core archive")
    core_inputs = [str(core)]
else:
    core_inputs = [str(source / ("src/" + name + ".cpp")) for name in
                   ("ethernet", "ipv4", "tcp", "config", "scheduler", "appliance")]
arguments = ["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
             "-isystem", str(sdk / "headers/usr/include/dpdk"),
             "-isystem", str(sdk / ("headers/usr/include/" + triplet + "/dpdk")),
             "-I", str(source / "include"), str(source / "backends/dpdk/ring_check.cpp"),
             *core_inputs,
             "-Wl,--no-as-needed", *libraries,
             "-Wl,--as-needed", "-pthread", "-ldl", "-o", str(output)]
for directory in sorted({str(pathlib.Path(path).parent) for path in libraries}):
    arguments.append("-Wl,-rpath," + directory)
subprocess.run(arguments, check=True, timeout=120)
print("PASS: real DPDK ring-PMD acceptance binary compiled", flush=True)
