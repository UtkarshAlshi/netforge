# Policy interchange version1

`nf-xdp-compile CONFIG INGRESS OUTPUT PORT=IFINDEX ... --portable` exports an
architecture-independent representation. `nf-xdp-compile --decode-policy INPUT
OUTPUT` converts it to the local native nf_policy ABI used by existing loaders.
Default compilation remains native ABI. Loaders do not autodetect interchange
files; use the explicit conversion step. Interface indices are namespace-local
and must be rebound/recompiled for deployment on a different namespace/host.

Layout, with unsigned big-endian integers and no padding:

- Eight bytes: ASCII NFPOL followed by00 00 01.
- Three u32: rule count, route count, default allow; six ingress MAC bytes.
- Each active rule: u32 ingress/source/destination; u16 source/destination ports;
  u8 source prefix/destination prefix/protocol/allow.20 bytes per rule.
- Each active route: u32 network/ifindex; u16 MTU; u8 prefix; six source MAC bytes
  and six destination MAC bytes.23 bytes per route.

Total length must be exactly26+20*rules+23*routes, at most1034 bytes. Bounds are
32 rules/16 routes. Decoder rejects unknown version, excess/truncated bytes,
invalid prefix/canonical network/protocol/allow values and invalid route index/MTU.
Unused native fields/padding are zero-initialized. No native struct is copied
from interchange input. Invalid input is rejected before destination opening.

This format does not authenticate or sign a policy and does not serialize
generation/config identity. Use authenticated configuration publication for
managed changes. It preserves the existing restricted feature semantics and
does not add NAT, stateful processing or automatic punt fallback.
