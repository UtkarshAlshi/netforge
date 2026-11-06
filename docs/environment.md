# Environment record

## Development targets

The supported execution target for M0 onward is Linux. Network namespaces, veth links, XDP, and DPDK are Linux facilities; macOS is not an equivalent substitute for dataplane validation.

Recommended baseline: an Ubuntu LTS or Fedora VM with four or more cores, 16 GB RAM, `iproute2`, `tcpdump`, Wireshark, `clang`, `cmake`, `ninja-build`, and Linux headers. A second Linux host is deferred until physical-NIC or credible end-to-end performance experiments begin.

## Local validation record — 2026-09-14

- Host OS: macOS/Darwin arm64
- CMake: unavailable
- C++ toolchain: present, but its installed standard-library headers are incomplete (`bits/c++config.h` missing).
- Result: source review and shell-script inspection were performed here; Linux compilation and namespace integration are intentionally not claimed as completed.

This document must be updated whenever benchmarks or integration tests are run. Results without an environment record are not valid performance evidence.

