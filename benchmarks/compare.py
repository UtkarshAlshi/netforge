#!/usr/bin/env python3
"""Counterbalanced linear/indexed trials with a shared binary and bounded budget."""
import argparse
import hashlib
import json
import pathlib
import platform
import statistics
import subprocess
import time
import sys
sys.dont_write_bytecode = True
from mixed import frame


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--iterations", type=int, default=5000)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--seconds-budget", type=int, default=90)
    parser.add_argument("--build-description", required=True)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 50000 or not 3 <= args.repetitions <= 9 or not 5 <= args.seconds_budget <= 120:
        parser.error("iterations 1..50000; repetitions 3..9; seconds 5..120")
    binary = args.binary.resolve(strict=True)
    root = args.output_dir
    root.mkdir(mode=0o700)
    base = (pathlib.Path(__file__).resolve().parent.parent / "examples/appliance.conf").read_text()
    base = base.replace("limits 1024 30000", "limits 4096 30000").replace("40000 40003", "40000 40255")
    configs = {}
    for name, flag in [("linear", "off"), ("indexed", "on")]:
        path = root / (name + ".conf")
        path.write_text(base + "\nflow-index " + flag + "\n")
        configs[name] = path
    deadline = time.monotonic() + args.seconds_budget
    cases, total = [], 0
    with (root / "raw.jsonl").open("x") as raw:
        for count in [1, 64, 256]:
            for size in [64, 128, 512, 1500]:
                corpus = root / f"udp-{count}-{size}.hex"
                corpus.write_text("".join(frame(size, 10000 + i).hex() + "\n" for i in range(count)))
                for mode in ["cold", "warm"]:
                    timings, ratios = {"linear": [], "indexed": []}, []
                    iterations = count if mode == "cold" else args.iterations
                    for repetition in range(args.repetitions):
                        order = ["linear", "indexed"] if repetition % 2 == 0 else ["indexed", "linear"]
                        pair = {}
                        for variant in order:
                            remaining = deadline - time.monotonic()
                            if remaining <= 0:
                                raise RuntimeError("comparison budget exhausted; partial raw evidence retained")
                            process = subprocess.run([str(binary), "bench-mix", str(configs[variant]), "lan", str(corpus), str(iterations), mode],
                                                     capture_output=True, text=True, check=True, timeout=min(10, remaining))
                            sample = json.loads(process.stdout)
                            assert sample["elapsed_ns"] > 0 and sample["emitted"] == iterations and sample["dropped"] == 0
                            assert sample["flow_count"] == count
                            assert sample["new_udp_mappings"] == (count if mode == "cold" else 0)
                            assert sample["tracked_udp"] == (0 if mode == "cold" else iterations)
                            pair[variant] = sample
                            timings[variant].append(iterations * 1e9 / sample["elapsed_ns"])
                            raw.write(json.dumps({**sample, "variant": variant, "flows": count,
                                                  "ethernet_bytes": size, "repetition": repetition, "order": order}) + "\n")
                            raw.flush(); total += 1
                        for field in ["iterations", "emitted", "dropped", "input_bytes", "new_udp_mappings", "tracked_udp", "flow_count"]:
                            assert pair["linear"][field] == pair["indexed"][field], "variant workload mismatch"
                        ratios.append(pair["linear"]["elapsed_ns"] / pair["indexed"]["elapsed_ns"])
                    cases.append({"flows": count, "ethernet_bytes": size, "mode": mode,
                                  "median_paired_indexed_to_linear_rate_ratio": statistics.median(ratios),
                                  "core_invocations_per_second": {variant: {"median": statistics.median(values),
                                      "min": min(values), "max": max(values), "sample_stdev": statistics.stdev(values)}
                                      for variant, values in timings.items()}})
    report = {"kind": "offline_counterbalanced_flow_index_comparison", "trials": total, "cases": cases,
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "platform": platform.platform(),
              "config_hashes": {key: hashlib.sha256(path.read_bytes()).hexdigest() for key, path in configs.items()},
              "build_description": args.build_description,
              "limitations": ["offline engine only; no network throughput or latency claim", "CPU throttling/background load remain uncontrolled",
                              "cold tiny batches have substantial timer noise", "odd repetitions leave one extra linear-first pair",
                              "descriptive paired ratios, not confidence intervals or significance", "fixed time/no expiry pressure",
                              "additional index memory is not measured by these timing trials"]}
    (root / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {total} verified counterbalanced offline trials")
    for case in cases:
        if case["mode"] == "warm" and case["ethernet_bytes"] == 64:
            print(f"flows={case['flows']} warm64 median paired indexed/linear rate={case['median_paired_indexed_to_linear_rate_ratio']:.3f}")


if __name__ == "__main__":
    main()
