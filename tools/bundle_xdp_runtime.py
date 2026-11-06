#!/usr/bin/env python3
"""Bundle the small native loader and its libraries for the matching lab image."""
import pathlib
import re
import shutil
import subprocess
import sys
binary, program, output = map(pathlib.Path, sys.argv[1:])
output.mkdir(mode=0o755)
output.chmod(0o755)
files = [(binary, output / "nf-xdp-check"), (program, output / "program.bpf.o")]
pending, libraries = [binary], {}
while pending:
    path = pending.pop()
    result = subprocess.run(["ldd", str(path)], capture_output=True, text=True, timeout=5, check=True)
    if "not found" in result.stdout:
        raise SystemExit("Missing native loader dependency")
    for value in re.findall(r"=>\s+(/[^\s]+)", result.stdout):
        dependency = pathlib.Path(value)
        if dependency.name.startswith(("libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux")):
            continue
        if dependency.name not in libraries:
            libraries[dependency.name] = dependency
            pending.append(dependency)
files += [(source, output / "lib" / name) for name, source in libraries.items()]
size = sum(source.stat().st_size for source, _ in files)
if size > 32 * 1048576:
    raise SystemExit("Loader/runtime bundle exceeds 32 MiB")
for source, destination in files:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
(output / "nf-xdp-check").chmod(0o755)
print(f"PASS: XDP runtime bundled bytes={size}")
