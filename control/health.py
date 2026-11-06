#!/usr/bin/env python3
"""Bounded TCP reachability observations and conservative backend-drain proposals.

Does not publish, run continuously, or change networking. Operators review the
candidate and deliver it through the acknowledged management API/fleet client.
"""
import argparse
import concurrent.futures
import ipaddress
import json
import pathlib
import socket
import subprocess

MAX_ENDPOINTS = 64


def endpoints(text):
    result = set()
    count = 0
    for line in text.splitlines():
        fields = line.split("#", 1)[0].split()
        if not fields or fields[0] != "tcp-service":
            continue
        for token in fields[4:]:
            if token.startswith("policy="):
                continue
            count += 1
            if count > MAX_ENDPOINTS:
                raise ValueError("health checking is limited to 64 backend slots")
            endpoint = token.removesuffix("/drain").split("@")[0]
            host, port = endpoint.split(":")
            ipaddress.IPv4Address(host)
            if not 1 <= int(port) <= 65535:
                raise ValueError("invalid TCP endpoint")
            result.add(endpoint)
            if len(result) > MAX_ENDPOINTS:
                raise ValueError("health checking is limited to 64 distinct TCP endpoints")
    return sorted(result)


def probe(endpoint, timeout):
    host, port = endpoint.split(":")
    try:
        with socket.create_connection((host, int(port)), timeout=timeout):
            return True
    except OSError:
        return False


def observe(text, previous, observations, generation, fall=3, rise=2):
    """Pure state transition. Manually drained backends are never re-enabled."""
    if type(generation) is not int or generation < 1 or not 1 <= fall <= 10 or not 1 <= rise <= 10:
        raise ValueError("invalid generation or thresholds")
    targets = endpoints(text)
    current = next((int(f[1]) for line in text.splitlines()
                    if (f := line.split("#", 1)[0].split()) and f[0] == "version"), None)
    if current is None or generation <= current:
        raise ValueError("candidate generation must increase")
    if set(observations) != set(targets) or any(type(v) is not bool for v in observations.values()):
        raise ValueError("complete boolean observations required")
    if not isinstance(previous, dict) or set(previous) - {"version", "endpoints", "managed"} or previous.get("version", 1) != 1:
        raise ValueError("invalid health-state schema")
    history = previous.get("endpoints", {})
    slots = {"|".join(fields[1:4] + [token.removesuffix("/drain").split("@")[0]])
             for line in text.splitlines()
             if (fields := line.split("#", 1)[0].split()) and fields[0] == "tcp-service"
             for token in fields[4:] if not token.startswith("policy=")}
    if not isinstance(previous.get("managed", []), list) or any(not isinstance(x, str) for x in previous.get("managed", [])):
        raise ValueError("invalid health ownership")
    managed = set(previous.get("managed", [])) & slots
    if not isinstance(history, dict) or len(history) > MAX_ENDPOINTS or len(previous.get("managed", [])) > MAX_ENDPOINTS:
        raise ValueError("invalid health-state bounds")
    updated, changes = {}, []
    for endpoint in targets:
        old = history.get(endpoint, {"successes": 0, "failures": 0})
        if not isinstance(old, dict) or set(old) != {"successes", "failures"} or any(type(n) is not int or not 0 <= n <= 10 for n in old.values()):
            raise ValueError("invalid health counters")
        good = observations[endpoint]
        updated[endpoint] = {"successes": min(10, old["successes"] + 1) if good else 0,
                             "failures": 0 if good else min(10, old["failures"] + 1)}
    lines = []
    for line in text.splitlines():
        body, marker, comment = line.partition("#")
        fields = body.split()
        if fields and fields[0] == "version":
            fields[1] = str(generation)
        elif fields and fields[0] == "tcp-service":
            for index in range(4, len(fields)):
                token = fields[index]
                if token.startswith("policy="):
                    continue
                endpoint = token.removesuffix("/drain").split("@")[0]
                drained = token.endswith("/drain")
                state = updated[endpoint]
                slot = "|".join(fields[1:4] + [endpoint])
                if not drained and state["failures"] >= fall:
                    fields[index] = token + "/drain"
                    managed.add(slot)
                    changes.append({"endpoint": endpoint, "action": "drain", "slot": slot})
                elif drained and slot in managed and state["successes"] >= rise:
                    fields[index] = token.removesuffix("/drain")
                    changes.append({"endpoint": endpoint, "action": "recover", "slot": slot})
        if fields:
            lines.append(" ".join(fields) + ((" #" + comment) if marker else ""))
        else:
            lines.append(line)
    # Remove ownership only after all occurrences have been considered. State
    # should be used with the exact previously accepted candidate configuration.
    for change in changes:
        if change["action"] == "recover":
            managed.discard(change["slot"])
    return "\n".join(lines) + "\n", {"version": 1, "endpoints": updated, "managed": sorted(managed)}, changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="trusted NetForge checker executable")
    parser.add_argument("--config", required=True)
    parser.add_argument("--previous-state")
    parser.add_argument("--output-dir", required=True, help="new private directory; never overwrites prior proposals")
    parser.add_argument("--generation", required=True, type=int)
    parser.add_argument("--allow-cidr", required=True, action="append", help="explicit backend address scope")
    parser.add_argument("--timeout-ms", type=int, default=200)
    parser.add_argument("--fall", type=int, default=3)
    parser.add_argument("--rise", type=int, default=2)
    args = parser.parse_args()
    if not 50 <= args.timeout_ms <= 500 or not 1 <= args.fall <= 10 or not 1 <= args.rise <= 10:
        parser.error("timeout must be 50..500 ms and thresholds 1..10")
    with pathlib.Path(args.config).open("rb") as source:
        data = source.read(65537)
    if len(data) > 65536:
        raise ValueError("config exceeds 64 KiB")
    result = subprocess.run([args.binary, "check", args.config], stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, timeout=5, check=False)
    if result.returncode:
        raise ValueError("NetForge rejected health-check source configuration")
    text = data.decode()
    networks = [ipaddress.IPv4Network(value, strict=True) for value in args.allow_cidr]
    targets = endpoints(text)
    for endpoint in targets:
        host = ipaddress.IPv4Address(endpoint.split(":")[0])
        if host.is_multicast or host.is_unspecified or host.is_loopback or not any(host in network for network in networks):
            raise ValueError("backend outside explicit health-check address scope")
    previous = {}
    if args.previous_state:
        with pathlib.Path(args.previous_state).open("rb") as source:
            raw = source.read(65537)
        if len(raw) > 65536:
            raise ValueError("state exceeds 64 KiB")
        previous = json.loads(raw)
    # Validate state/config before emitting even a TCP SYN.
    observe(text, previous, {target: False for target in targets}, args.generation, args.fall, args.rise)
    output = pathlib.Path(args.output_dir)
    output.mkdir(mode=0o700)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        futures = {target: executor.submit(probe, target, args.timeout_ms / 1000) for target in targets}
        observations = {target: future.result() for target, future in futures.items()}
    candidate, state, changes = observe(text, previous, observations, args.generation, args.fall, args.rise)
    for name, value in [("candidate.conf", candidate), ("state.json", json.dumps(state, indent=2) + "\n"),
                        ("report.json", json.dumps({"observations": observations, "changes": changes}, indent=2) + "\n")]:
        with (output / name).open("x") as target:
            target.write(value)
        (output / name).chmod(0o600)
    print(json.dumps({"endpoints": len(targets), "changes": changes, "published": False}))


if __name__ == "__main__":
    main()
