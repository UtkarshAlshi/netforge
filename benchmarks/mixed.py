#!/usr/bin/env python3
"""Bounded synthetic multi-flow core trials, not network throughput/latency."""
import argparse
import hashlib
import ipaddress
import json
import pathlib
import platform
import statistics
import struct
import subprocess
import time


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    value = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def frame(size, source_port):
    src = ipaddress.IPv4Address("10.77.0.2").packed
    dst = ipaddress.IPv4Address("10.77.0.100").packed
    udp = struct.pack("!HHHH", source_port, 9000, size - 34, 0) + bytes(size - 42)
    value = checksum(src + dst + b"\0\x11" + struct.pack("!H", len(udp)) + udp) or 65535
    udp = udp[:6] + struct.pack("!H", value) + udp[8:]
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, size - 14, 1, 0, 64, 17, 0, src, dst)
    ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
    return bytes.fromhex("02000000000102000000000a0800") + ip + udp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--iterations", type=int, default=5000)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--flows", default="1,64,256")
    parser.add_argument("--sizes", default="64,128,512,1500")
    parser.add_argument("--seconds-budget", type=int, default=60)
    parser.add_argument("--build-description", required=True, help="record compiler/flags and sanitizer state explicitly")
    args = parser.parse_args()
    flows = sorted(set(int(n) for n in args.flows.split(",")))
    sizes = sorted(set(int(n) for n in args.sizes.split(",")))
    if not flows or not sizes or len(flows) > 3 or len(sizes) > 4 or not all(1 <= n <= 256 for n in flows) or not all(n in [64, 128, 512, 1500] for n in sizes):
        parser.error("up to three flow counts 1..256 and four sizes from 64,128,512,1500")
    if not 1 <= args.iterations <= 50000 or not 3 <= args.repetitions <= 9 or not 5 <= args.seconds_budget <= 120:
        parser.error("iterations 1..50000, repetitions 3..9, seconds budget 5..120")
    binary = args.binary.resolve(strict=True)
    root = args.output_dir
    root.mkdir(mode=0o700)
    base = (pathlib.Path(__file__).resolve().parent.parent / "examples" / "appliance.conf").read_text()
    base = base.replace("limits 1024 30000", "limits 4096 30000").replace("40000 40003", "40000 40255")
    config = root / "appliance.conf"
    config.write_text(base)
    samples, cases = [], []
    deadline = time.monotonic() + args.seconds_budget
    with (root / "raw.jsonl").open("x") as raw:
        for count in flows:
            for size in sizes:
                corpus = root / f"udp-{count}-{size}.hex"
                corpus.write_text("".join(frame(size, 10000 + i).hex() + "\n" for i in range(count)))
                for mode in ["cold", "warm"]:
                    trials = []
                    iterations = count if mode == "cold" else args.iterations
                    for repeat in range(args.repetitions):
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise RuntimeError("global benchmark budget exhausted; raw partial trials retained")
                        result = subprocess.run([str(binary), "bench-mix", str(config), "lan", str(corpus), str(iterations), mode],
                                                capture_output=True, text=True, check=True, timeout=min(10, remaining))
                        trial = json.loads(result.stdout)
                        if trial["elapsed_ns"] <= 0 or trial["emitted"] != iterations or trial["dropped"] or trial["flow_count"] != count:
                            raise RuntimeError("workload correctness check failed")
                        if mode == "cold" and trial["new_udp_mappings"] != count:
                            raise RuntimeError("cold workload did not create the expected mappings")
                        if mode == "warm" and (trial["new_udp_mappings"] or trial["tracked_udp"] != iterations):
                            raise RuntimeError("warm workload did not use established mappings")
                        trial.update({"configured_flows": count, "ethernet_bytes": size, "repeat": repeat})
                        raw.write(json.dumps(trial) + "\n"); raw.flush()
                        samples.append(trial); trials.append(iterations * 1e9 / trial["elapsed_ns"])
                    cases.append({"flows": count, "ethernet_bytes": size, "mode": mode,
                                  "core_invocations_per_second": {"median": statistics.median(trials), "min": min(trials),
                                                                  "max": max(trials), "sample_stdev": statistics.stdev(trials)}})
    report = {"kind": "offline_mixed_udp_core_trials", "platform": platform.platform(), "architecture": platform.machine(),
              "build_description": args.build_description, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "config_sha256": hashlib.sha256(config.read_bytes()).hexdigest(), "trials": len(samples), "cases": cases,
              "limitations": ["no socket I/O, network throughput or latency measurement", "cold tiny batches are sensitive to timer noise",
                              "fixed synthetic arrival order/time; no expiry, receive queue or congestion model",
                              "warmup populates every supplied flow; each trial starts a fresh process",
                              "frame sizes exclude Ethernet FCS/preamble/gap", "CPU frequency/background load are not controlled",
                              "descriptive variation only, no significance or confidence claim", "no physical-NIC or high-performance inference"]}
    with (root / "summary.json").open("x") as output:
        json.dump(report, output, indent=2); output.write("\n")
    print(f"PASS: {len(samples)} verified offline trials; saved {root / 'summary.json'}")


if __name__ == "__main__":
    main()
