#!/usr/bin/env python3
"""Finite health reconciliation through the acknowledged local agent API."""
import argparse
import concurrent.futures
import hashlib
import http.client
import ipaddress
import json
import pathlib
import random
import subprocess
import time
import urllib.parse
from agent import atomic
from health import endpoints, observe, probe


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


class API:
    def __init__(self, url, token_file):
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme != "http" or parsed.hostname != "127.0.0.1" or not parsed.port or parsed.path not in ("", "/") or parsed.username or parsed.query or parsed.fragment:
            raise ValueError("use an explicit loopback agent port or secure forwarded endpoint")
        self.port = parsed.port
        with pathlib.Path(token_file).open() as source:
            self.token = source.read(130).strip()
        if not 16 <= len(self.token) <= 128 or any(not (c.isalnum() or c in "_-") for c in self.token):
            raise ValueError("invalid agent credential")

    def request(self, method, path, value=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=6)
        try:
            body = json.dumps(value, ensure_ascii=False).encode() if value is not None else None
            if body is not None and len(body) > 80000:
                raise ValueError("publication request exceeds 80000 bytes")
            connection.request(method, path, body=body, headers={"Authorization": "Bearer " + self.token,
                                                                "Content-Type": "application/json"})
            response = connection.getresponse()
            raw = response.read(450001)
            if len(raw) > 450000:
                raise RuntimeError("agent response exceeds bounds")
            result = json.loads(raw)
            if response.status != 200:
                raise RuntimeError("agent request failed: " + str(result.get("error", response.status)))
            return result
        finally:
            connection.close()


def bounded_json(path):
    with pathlib.Path(path).open("rb") as source:
        raw = source.read(65537)
    if len(raw) > 65536:
        raise ValueError("health state exceeds 64 KiB")
    return json.loads(raw)


def run_round(api, binary, networks, output, previous=None, fall=3, rise=2, timeout=0.2, observer=None):
    if not 1 <= fall <= 10 or not 1 <= rise <= 10 or not 0.05 <= timeout <= 0.5:
        raise ValueError("invalid health thresholds/deadline")
    networks = [ipaddress.IPv4Network(value, strict=True) for value in networks]
    if not networks:
        raise ValueError("explicit health-check scope required")
    snapshot = api.request("GET", "/v1/config")
    text, generation = snapshot["config"], snapshot["generation"]
    if not isinstance(text, str) or not 1 <= len(text.encode()) <= 65536 or type(generation) is not int or not 1 <= generation < 0xffffffff:
        raise ValueError("invalid configuration snapshot")
    state = {}
    if previous:
        previous = pathlib.Path(previous)
        accepted = bounded_json(previous / "accepted.json")
        if accepted.get("sha256") != digest(text) or accepted.get("generation") != generation:
            raise RuntimeError("configuration changed outside this health controller; reconcile ownership explicitly")
        state = bounded_json(previous / "state.json")
    targets = endpoints(text)
    for target in targets:
        ip = ipaddress.IPv4Address(target.split(":")[0])
        if ip.is_loopback or ip.is_multicast or ip.is_unspecified or not any(ip in network for network in networks):
            raise ValueError("backend outside explicit health-check scope")
    # Pure validation before any probes; the real checker validates source too.
    observe(text, state, {target: False for target in targets}, generation + 1, fall, rise)
    output = pathlib.Path(output)
    output.mkdir(mode=0o700)
    atomic(output / "source.conf", text.encode())

    def check(path):
        result = subprocess.run([str(binary), "check", str(path)], stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, timeout=5, check=False)
        if result.returncode:
            raise ValueError("NetForge rejected health configuration")
        return json.loads(result.stdout)["generation"]

    if check(output / "source.conf") != generation:
        raise ValueError("snapshot generation does not match configuration")
    if observer:
        observations = observer(targets, timeout)
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            futures = {target: executor.submit(probe, target, timeout) for target in targets}
            observations = {target: future.result() for target, future in futures.items()}
    candidate, next_state, changes = observe(text, state, observations, generation + 1, fall, rise)
    atomic(output / "candidate.conf", candidate.encode())
    if check(output / "candidate.conf") != generation + 1:
        raise ValueError("invalid candidate generation")
    # Persist diagnostics before sending. Without accepted.json this round must
    # never seed the next reconciliation, even if the POST may have succeeded.
    report = {"observations": observations, "changes": changes, "published": False}
    atomic(output / "report.json", json.dumps(report, indent=2).encode())
    accepted_text = text
    if changes:
        result = api.request("POST", "/v1/config", {"expected_generation": generation, "config": candidate})
        if result.get("generation") != generation + 1 or not result.get("running") or result.get("uncertain"):
            raise RuntimeError("health publication not acknowledged; reconcile before retrying")
        generation += 1
        accepted_text = candidate
        report["published"] = True
    else:
        # No revision churn, but detect a concurrent publisher before accepting
        # observation state. Later races are detected by the next round's hash.
        confirm = api.request("GET", "/v1/config")
        if confirm != snapshot:
            raise RuntimeError("configuration changed while checking health")
    atomic(output / "state.json", json.dumps(next_state, indent=2).encode())
    atomic(output / "report.json", json.dumps(report, indent=2).encode())
    # Export only after an acknowledged publication or confirmed unchanged snapshot.
    # Endpoint count is already bounded by observe(); labels are parsed IPv4:port.
    metrics = [f"netforge_health_config_generation {generation}",
               f"netforge_health_round_published {int(report['published'])}",
               f"netforge_health_endpoints {len(observations)}"]
    for endpoint, good in sorted(observations.items()):
        counts = next_state["endpoints"][endpoint]
        metrics.extend([f'netforge_health_probe_up{{endpoint="{endpoint}"}} {int(good)}',
                        f'netforge_health_success_streak{{endpoint="{endpoint}"}} {counts["successes"]}',
                        f'netforge_health_failure_streak{{endpoint="{endpoint}"}} {counts["failures"]}'])
    atomic(output / "metrics.prom", ("\n".join(metrics) + "\n").encode())
    atomic(output / "accepted.json", json.dumps({"generation": generation, "sha256": digest(accepted_text)}).encode())
    return {"generation": generation, "published": report["published"], "changes": changes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--token-file", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--allow-cidr", action="append", required=True)
    parser.add_argument("--output-dir", required=True, help="new private run directory")
    parser.add_argument("--previous-round")
    parser.add_argument("--fall", type=int, default=3)
    parser.add_argument("--rise", type=int, default=2)
    parser.add_argument("--timeout-ms", type=int, default=200)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--interval", type=float, default=5)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 20 or not 1 <= args.interval <= 30 or not 50 <= args.timeout_ms <= 500 or not 1 <= args.fall <= 10 or not 1 <= args.rise <= 10:
        parser.error("rounds 1..20, interval 1..30 seconds, timeout 50..500 ms, thresholds 1..10")
    api = API(args.url, args.token_file)
    root = pathlib.Path(args.output_dir)
    root.mkdir(mode=0o700)
    previous = args.previous_round
    for index in range(args.rounds):
        output = root / f"round-{index + 1:03d}"
        result = run_round(api, args.binary, args.allow_cidr, output, previous,
                           args.fall, args.rise, args.timeout_ms / 1000)
        print(json.dumps(result), flush=True)
        previous = output
        if index + 1 < args.rounds:
            time.sleep(args.interval * random.uniform(0.9, 1.1))


if __name__ == "__main__":
    main()
