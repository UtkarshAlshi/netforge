# Retained portable0.1.0 evidence

These are small, sanitized reports from actual execution, committed so they do
not disappear when hosted CI artifacts expire. No executable, deployment token,
private state or packet capture is included.

| File | Purpose |
|---|---|
| [provenance.json](provenance.json) | Exact engine source commit, workflow, artifact/raw hashes and environment |
| [network-trials.json](network-trials.json) |48 trials with raw per-packet latency, counts and binary/config hashes |
| [summary.json](summary.json) | Descriptive per-condition medians derived from those raw observations |
| [observations.md](observations.md) | Human-readable rates/latency table with explicit interpretation limits |

Observed:48 complete trials ×64 packets =3,072 verified packets;0 missing.
Four Ethernet frame sizes64/128/512/1500, bursts1/4/16, two repetitions per
backend/condition. The backends are reference AF_PACKET and real DPDK AF_PACKET
PMD on private veths under a shared0.5CPU/512MiB cap.

Regenerate the summary from the repository root into a new directory:

```sh
python3 tools/summarize_network.py results/portable-0.1.0/network-trials.json ../netforge-results-check
```

To reproduce actual traffic, use the isolated hosted workflow described in
[the live-lab guide](../../docs/live-lab.md). Running the summary does not rerun
traffic. Source changes after the recorded commit must be tested separately.
The presentation refresh leaves packet-engine behavior unchanged, but does not
pretend these measurements were taken at every later commit.
