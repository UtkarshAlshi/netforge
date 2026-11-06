# ADR 0001: Use C++20 for the initial baseline

**Status:** Accepted — 2026-09-14

The baseline dataplane and packet labs use C++20 with a small standard-library surface. This fits the learning goal: explicit byte handling and ownership are visible, while `std::span`, value types, RAII, and sanitizers reduce accidental errors.

This does not impose a C++ abstraction on XDP C programs or DPDK APIs. Their native interfaces will remain explicit.

## Consequences

- Build tooling is CMake plus compiler warnings and optional ASan/UBSan.
- No parser dependency or packet framework is used at M1.
- Packet parsing functions return explicit success/error results instead of throwing on malformed input.

