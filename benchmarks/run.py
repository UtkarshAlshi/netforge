"""Repeated offline core measurements; these are not NIC/network benchmarks."""
import argparse
import datetime
import hashlib
import json
import pathlib
import platform
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("binary", type=pathlib.Path)
parser.add_argument("config", type=pathlib.Path)
parser.add_argument("frames", type=pathlib.Path)
parser.add_argument("--ingress", default="lan")
parser.add_argument("--iterations", type=int, default=10000)
parser.add_argument("--repetitions", type=int, default=7)
parser.add_argument("--output", type=pathlib.Path, required=True)
args = parser.parse_args()
if not 1 <= args.iterations <= 1000000 or not 3 <= args.repetitions <= 30:
    parser.error("iterations 1..1000000; repetitions 3..30")
samples = []
for _ in range(args.repetitions):
    result = subprocess.run([str(args.binary.resolve()), "bench", str(args.config), args.ingress,
                             str(args.frames), str(args.iterations)], capture_output=True, text=True,
                            timeout=60, check=True)
    sample = json.loads(result.stdout)
    if sample["emitted"] != args.iterations or sample["dropped"]:
        raise SystemExit("Refusing to report throughput for a dropping workload")
    samples.append(sample)
rates = [sample["iterations"] * 1e9 / sample["elapsed_ns"] for sample in samples]
report = {
    "kind": "offline_core_microbenchmark",
    "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "platform": platform.platform(), "machine": platform.machine(),
    "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
    "config_sha256": hashlib.sha256(args.config.read_bytes()).hexdigest(),
    "frames_sha256": hashlib.sha256(args.frames.read_bytes()).hexdigest(),
    "samples": samples,
    "packets_per_second": {"median": statistics.median(rates), "min": min(rates),
                           "max": max(rates), "sample_stdev": statistics.stdev(rates)},
    "limitations": ["first frame only; repeated established-flow workload", "single process per trial",
                    "includes allocation and core processing; excludes packet I/O", "no network latency or line-rate claim",
                    "frequency/background load not controlled by this harness", "record compiler/build flags separately"],
}
with args.output.open("x") as output:
    json.dump(report, output, indent=2)
    output.write("\n")
print(f"Saved {len(samples)} raw trials and summary to {args.output}")
