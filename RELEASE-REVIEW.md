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
artifacts. Hosted run42224602962 passed Linux/macOS native verification and real DPDK,
worker crash recovery and packet-socket pressure acceptance. Subsequent source
changes require their own passing run. Draft PR1 remains unmerged.

The maintained portable entry point is `python3 tools/verify.py --sanitizers
--output /absolute/new/output`. It performs serial build, bounded CTest, lab-plan
safety, manifest packaging/extraction and offline demo without installation,
Docker execution or NIC changes. CMake3.20+, a C++20 compiler, Python3 and
OpenSSL CLI are prerequisites. Hosted CI also verifies from a fresh Ubuntu image
using tools/hosted_dpdk.sh under a512-MiB/no-swap/.5-CPU/128-PID cap. Dependencies
are fetched through Ubuntu repositories there; this is not a fully offline or
bit-identical dependency lock. Optional XDP tests still require the documented
Linux capabilities and kernel support.
