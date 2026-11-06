# Bounded operator demo

From a source checkout with a built reference binary:

```sh
python3 tools/run_demo.py build/netforge /absolute/path/new-demo-directory
```

No Docker, raw socket privileges, network interfaces, downloaded dependencies or
service processes are required by the demo itself. It uses the standard-library
fixture generator and validates five synthetic replay cases in separate engine
invocations: routing/TTL decrement, SNAT tuple, service round-robin and same-flow
affinity, ICMP TTL expiry, and malformed-frame rejection. Emitted IPv4 and UDP/ICMP
checksums are independently checked. Output is exclusively created and retained.

The report records binary/config SHA256, packet count and reason codes. Each case
retains input hex, output JSON and metrics. No packet capture, credentials or live
endpoint is involved. It demonstrates reference behavior, not live delivery,
performance, full backend parity or all-roadmap completion.

In the existing Mac lab, operator-demo-batch.sh reuses the source-checked verified
Linux binary in the capped builder. It installs/builds nothing and changes no
networking. This is the demo entry point; the historical acceptance scripts in
netforge-live remain separate evidence, not a fresh-lab provisioning system.
