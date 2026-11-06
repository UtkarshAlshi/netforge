#!/usr/bin/env python3
"""Build a bounded Python runtime bundle for the matching Ubuntu lab image only."""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import sysconfig

LIMIT = 64 * 1024 * 1024


def main():
    if sys.platform != "linux" or len(sys.argv) != 2:
        raise SystemExit("Usage (Linux build container): bundle_python.py NEW_OUTPUT_DIRECTORY")
    destination = pathlib.Path(sys.argv[1])
    if destination.exists():
        raise SystemExit("Output already exists; choose a new directory")
    standard = pathlib.Path(sysconfig.get_path("stdlib")).resolve()
    binary = pathlib.Path(sys.executable).resolve()
    version = f"python{sys.version_info.major}.{sys.version_info.minor}"
    ignored = {"__pycache__", "test", "tests", "idlelib", "tkinter", "ensurepip", "venv", "site-packages", "dist-packages"}
    files = [(binary, pathlib.Path("bin/python3"))]
    for directory, names, leaves in os.walk(standard, followlinks=False):
        names[:] = [n for n in names if n not in ignored]
        for leaf in leaves:
            path = pathlib.Path(directory) / leaf
            if path.is_file() and path.suffix != ".pyc":
                files.append((path, pathlib.Path("lib") / version / path.relative_to(standard)))
    # Dependencies must come from this image. Keep its existing glibc/loader;
    # use the bundle only in the matching lab image, never on macOS.
    libraries = {}
    pending = [binary] + [p for p, _ in files if p.suffix == ".so"]
    visited = set()
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        result = subprocess.run(["ldd", str(path)], capture_output=True, text=True, timeout=5)
        if "not found" in result.stdout:
            raise RuntimeError("Unresolved shared dependency: " + str(path))
        for name in re.findall(r"=>\s+(/[^\s]+)", result.stdout):
            dependency = pathlib.Path(name)
            if dependency.name.startswith(("libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux")):
                continue
            if dependency.name not in libraries:
                libraries[dependency.name] = dependency
                pending.append(dependency)
    files += [(path, pathlib.Path("lib") / name) for name, path in libraries.items()]
    size = sum(path.stat().st_size for path, _ in files)
    if size > LIMIT:
        raise SystemExit(f"Refusing bundle over 64 MiB: {size} bytes")
    # Public runtime code, not secrets: copied UID can differ in a cap-dropped lab.
    destination.mkdir(mode=0o755)
    destination.chmod(0o755)
    for source, relative in files:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    print(f"BUNDLED files={len(files)} bytes={size} python={version}", flush=True)
    environment = {**os.environ, "PYTHONHOME": str(destination), "LD_LIBRARY_PATH": str(destination / "lib"), "PYTHONDONTWRITEBYTECODE": "1"}
    subprocess.run([str(destination / "bin/python3"), "-S", "-c",
                    "import http.server, secrets, subprocess, threading, hashlib; print('PASS: bundled control-plane imports')"],
                   env=environment, check=True, timeout=10)


if __name__ == "__main__":
    main()
