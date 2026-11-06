# Getting started

## Supported environments

| Task | macOS | Linux |
|---|---|---|
| Build, portable replay, browser demo and core tests | Supported | Supported |
| AF_PACKET reference/worker traffic | Not available | Dedicated isolated lab only |
| XDP and DPDK backend acceptance | Linux VM/container required | Kernel/tools/capabilities required |

Windows-native execution is not validated. Use a suitable Linux environment.
Python tools use the standard library; no npm, pip, database or cloud account is
required for the portable demo. The demo is a local HTML file, not a web server.

## 1. Check prerequisites

```sh
cmake --version
c++ --version
python3 --version
openssl version
```

For the demo, use C++20 (Clang or GCC) and Python3.9+. If CMake is missing,
the launcher compiles the reference executable directly and serially. The full
verification suite requires CMake3.20+, Python3.10+ and Make/Ninja. OpenSSL CLI is
used by the full TLS verification tests. Install missing prerequisites through
your normal development environment. For example, Ubuntu24.04 users can install
`build-essential cmake python3 openssl`; macOS users need Xcode Command Line Tools
and CMake/Python from their preferred package manager. These commands are checks;
NetForge does not install prerequisites or alter your shell configuration.

## 2. Clone and run the smallest useful demonstration

```sh
git clone https://github.com/UtkarshAlshi/netforge.git
cd netforge
python3 tools/demo.py --output ../netforge-demo
```

The build is serial and targets only `netforge`; CMake is used if available,
otherwise your existing `c++` compiler is used. To explicitly select the latter:

```sh
python3 tools/demo.py --build-system direct --output ../netforge-demo-direct
```

 Expect `PASS: five offline demo
cases` followed by an HTML path. Open `../netforge-demo/packets/index.html` in a
browser. Files remain local; no packets are sent and no interface is changed.

## 3. Run the complete portable verification

```sh
python3 tools/verify.py --sanitizers --output ../netforge-verification
```

Inspect `../netforge-verification/report.json`: `complete` must be `true`.
Individual `step-*.log` files retain compiler/test failures. The output also holds
`source.tar.gz`, a verified extraction and `demo/index.html`. This step validates
portable code and loopback control tests; optional real Linux backend integration
runs separately in hosted CI. See [the workflow](../.github/workflows/ci.yml).

## 4. Build manually when developing

```sh
cmake -S . -B ../netforge-build -DNETFORGE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build ../netforge-build --parallel 1
ctest --test-dir ../netforge-build --output-on-failure --timeout 60
```

Keep a serial build on small machines. Sanitizers are for correctness; benchmark
an optimized build separately. The project does not configure swap or RAM limits
on your computer. Live-container limits are documented in the separate lab guide.

## Common issues

| Symptom | Meaning and next step |
|---|---|
| CMake missing | Demo automatically uses the direct compiler build; full verification still needs CMake |
| Compiler missing | Install your platform development tools; preflight creates no output directory |
| Output directory already exists | Choose a new sibling path, e.g. `../netforge-demo-2`; no automatic deletion occurs |
| `Output must be outside source` | Use a sibling path beginning `../`, not a directory inside the checkout |
| `run` unsupported on macOS | Use `check`/`replay`/the offline demo; live packet I/O requires Linux |
| TLS test cannot find `openssl` | Install OpenSSL CLI or adjust the executable search path for your development shell |
| A test fails | Preserve its log and exact source revision; do not claim a successful verification |

To remove a demo later, inspect and delete only the output directory you chose.
NetForge never prunes containers, volumes, images or unrelated files.

## Recovering from an older launcher failure

If an older launcher created `../netforge-demo` before failing on missing CMake,
leave that directory intact and use a new output path after updating the checkout:

```sh
git pull --ff-only
python3 tools/demo.py --output ../netforge-demo-2 && open ../netforge-demo-2/packets/index.html
```

`open` is macOS-specific. On Linux, open the printed HTML path in your browser.
Using `&&` avoids attempting to open a page if the build/demo fails. The `.npmrc`/
nvm shell warning is unrelated; NetForge has no Node/npm dependency. Keep the
small-machine disk reserve used by your lab; if space is low, inspect the
already-generated CI demo artifact instead of building locally.
