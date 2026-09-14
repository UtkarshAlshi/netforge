# ADR 0003: M1 records but does not validate IPv4 checksums

**Status:** Accepted — 2026-09-14

The initial decoder validates framing bounds, version, IHL, and declared total length. It intentionally does not claim checksum validation. Adding checksum code before a capture-driven parser workflow would confuse two concepts: structural safety and protocol integrity.

Checksum computation and validation become a separate M1 exercise with known-good and deliberately corrupted vectors. Router forwarding must not be built until this boundary is explicit.
