# Five-minute demo and presentation

This is the recommended interview demonstration. It requires no Docker, root
access, virtual interfaces or background service.

## Prepare

```sh
python3 tools/demo.py --output ../netforge-demo
```

Open `../netforge-demo/packets/index.html` in a browser. The page is self-contained
and uses no remote scripts, fonts or telemetry. For a repeated run, use a new
output directory. If you already built the binary, skip rebuilding:

```sh
python3 tools/demo.py --binary ../netforge-build/netforge --output ../netforge-demo-2
```

## Explain the five cases

1. **Routing:** point to the input/output tuple and TTL64→63. Explain why the IP
   destination remains the endpoint while Ethernet addresses change per hop.
2. **NAT:** identify source `10.77.0.2:12345` becoming `10.77.0.254:40000`. Explain
   that real return traffic uses stored inverse translation; this replay case
   illustrates the forward rewrite, while live reverse traffic is tested elsewhere.
3. **Load balancing:** packets from source ports10001,10002,10001 select backends
   .130,.131,.130. Explain per-flow selection and why the third packet stays pinned.
4. **TTL expiry:** identify the ICMP response and `ttl_exceeded` reason. Explain
   that forwarding invalid transit TTL would create a routing-loop problem.
5. **Malformed frame:** show that a one-byte Ethernet input produces a rejection
   and no emitted frame. Bounds checks precede deeper parsing.

Open “Inspect input and decision JSON” on a case to show that the UI is derived
from actual executable output. The checksums are independently validated in
Python, and the page records executable/config hashes. The page is not a live
network monitor or an assertion of performance.

## Show the command-line path

```sh
../netforge-demo/build/netforge check examples/appliance.conf
../netforge-demo/build/netforge replay examples/appliance.conf lan ../netforge-demo/packets/service.hex
```

Expected configuration result: `{"valid":true,"generation":1}`.
Expected service decisions: `new_udp_mapping`, `new_udp_mapping`, `tracked_udp`.
The complete JSON contains egress and transformed frame hex. Metrics are written
to stderr; redirect streams separately if you want to inspect them:

```sh
../netforge-demo/build/netforge replay examples/appliance.conf lan ../netforge-demo/packets/service.hex > ../netforge-demo/decisions.jsonl 2> ../netforge-demo/metrics.txt
python3 tools/metrics_view.py ../netforge-demo/metrics.txt ../netforge-demo/metrics.html
```

The metrics page is also an offline snapshot. Avoid interpreting core emissions
as successful wire delivery.

## Suggested narration

“NetForge separates packet policy from packet I/O. Here I use synthetic replay to
make transformations easy to inspect. The same reference engine runs through
Linux raw sockets. I tested actual UDP/TCP, reload and crash behavior on private
veths, and integrated restricted XDP/DPDK paths separately. Their capabilities
and performance boundaries differ; the results page makes those limits explicit.”

For real traffic, prepare [the isolated Linux lab](live-lab.md) separately rather
than changing a workstation interface during a presentation.
