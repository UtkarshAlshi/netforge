# Source review snapshot

This is a reviewable0.1.0-rc1 engineering snapshot, not certification of the full
original roadmap. STATUS.md, ROADMAP.md and VERIFICATION.md retain its boundaries.

Build on a Linux host with C++20, CMake, Python3 and Threads:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

Core tests require no NIC changes. Optional XDP/DPDK live acceptance uses the
separately documented capabilities, dependencies and restricted policy subsets.
Do not execute historical lab scripts blindly on an unrelated machine.

Create a source-only archive after any edits and manifest refresh:

```sh
python3 tools/package_source.py /absolute/path/new-netforge-source.tar.gz
```

The packager verifies every manifest-listed hash, refuses overwrites and escaped
or symlinked files, and caps input at16 MiB/1024 files. It includes no discovered
Docker state, tokens, private test directories, binaries or packet captures.
Archive metadata is deterministic. This packaging step neither publishes a
release nor proves clean reproduction on another computer.

For a retained evidence index, pass the sibling netforge-live directory and a new
output directory to tools/evidence_report.py. It reads bounded known logs and
marks missing evidence explicitly. Historical performance is source-specific
offline core timing, not physical NIC throughput or network latency.


Hosted CI workflow is prepared for Ubuntu24.04/macOS15 sanitizer builds, CTest,
lab-plan safety, source-package verification/extraction and bounded offline demo
artifacts. It has not been remotely executed. A repository destination and
publication authorization must be established before claiming hosted acceptance.
