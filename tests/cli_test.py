"""Subprocess-level checks; generated fixtures stay in the chosen build directory."""
import json
import pathlib
import struct
import subprocess
import sys

binary = pathlib.Path(sys.argv[1]).resolve()
project = pathlib.Path(sys.argv[2]).resolve()
cases = binary.parent / "cli-cases"
cases.mkdir(exist_ok=True)
config = project / "examples" / "appliance.conf"


def run(*args, ok=True):
    result = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True, timeout=15)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    return result


assert json.loads(run("check", config).stdout)["valid"]
assert json.loads(run("check", project / "examples" / "tcp-appliance.conf").stdout)["valid"]
draining = cases / "draining.conf"
draining.write_text(config.read_text().replace("10.77.0.130:9000", "10.77.0.130:9000/drain"))
assert json.loads(run("check", draining).stdout)["valid"]
for policy in ("round-robin", "weighted-round-robin", "least-connections", "rendezvous"):
    policy_file = cases / (policy + ".conf")
    contents = config.read_text().replace("service lan 10.77.0.100 9000 ", "service lan 10.77.0.100 9000 policy=" + policy + " ")
    if policy == "weighted-round-robin":
        contents = contents.replace("10.77.0.130:9000", "10.77.0.130:9000@3/drain")
    policy_file.write_text(contents)
    assert json.loads(run("check", policy_file).stdout)["valid"]
dynamic_file = cases / "dynamic.conf"
dynamic_file.write_text(config.read_text() + "\nneighbor-discovery on\n")
assert json.loads(run("check", dynamic_file).stdout)["valid"]
assert "0.1.0-rc1" in run("--version").stdout
assert "Usage:" in run(ok=False).stderr
for i, content in enumerate([
    config.read_text() + "\nunknown hello\n",
    config.read_text().replace("version 1", "version 0"),
    config.read_text().replace("default deny", "default surprise"),
    config.read_text() + "\nversion 2\n",
    config.read_text().replace("route 10.77.0.0/25", "route 10.77.0.1/25"),
    config.read_text().replace("limits 1024 30000", "limits 0 30000"),
    config.read_text() + "\ntcp-stateful maybe\n",
    config.read_text().replace("10.77.0.130:9000 10.77.0.131:9000", "10.77.0.130:9000 10.77.0.130:9000"),
    config.read_text().replace("10.77.0.130:9000", "10.77.0.130:9000/unknown"),
    config.read_text() + "\ntcp-timeouts 0 120000 5000\n",
    config.read_text() + "\ntcp-stateful on\ntcp-stateful off\n",
    config.read_text() + "\ntcp-service lan 10.77.0.100 9000 10.77.0.130:9000\n",
    config.read_text().replace("service lan 10.77.0.100 9000 ", "service lan 10.77.0.100 9000 policy=unknown "),
    config.read_text().replace("10.77.0.130:9000", "10.77.0.130:9000@2"),
    config.read_text() + "\nneighbor-discovery maybe\n",
    config.read_text() + "\nneighbor-discovery on\nneighbor-discovery off\n",
    config.read_text().replace("lan", "x" * 33),
    config.read_text() + "\nflow-index maybe\n",
    config.read_text() + "\nflow-index on\nflow-index off\n",
    "#" * 65537,
]):
    path = cases / f"invalid-{i}.conf"
    path.write_text(content)
    run("check", path, ok=False)


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    value = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


src = bytes([10, 77, 0, 2])
dst = bytes([10, 77, 0, 130])
payload = struct.pack("!HHHH", 12345, 9000, 11, 0) + b"abc"
ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 31, 7, 0, 64, 17, 0, src, dst)
ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
frame = bytes.fromhex("02000000000102000000000a0800") + ip + payload
frames = cases / "frames.hex"
frames.write_text(frame.hex() + "\n00\n")
replay = run("replay", config, "lan", frames)
decisions = [json.loads(line) for line in replay.stdout.splitlines()]
assert decisions[0]["reason"] == "new_udp_mapping" and decisions[0]["egress"] == "wan"
assert decisions[1]["reason"] == "ethernet_truncated" and not decisions[1]["egress"]
translated = bytes.fromhex(decisions[0]["frame"])
assert translated[26:30] == bytes([10, 77, 0, 254])
assert translated[22] == 63 and checksum(translated[14:34]) == 0
assert checksum(translated[26:34] + b"\0\x11" + struct.pack("!H", 11) + translated[34:]) == 0
run("replay", config, "wrong", frames, ok=False)
bad_hex = cases / "bad.hex"
bad_hex.write_text("zz\n")
run("replay", config, "lan", bad_hex, ok=False)
bad_hex.write_text("0" * 140001)
run("replay", config, "lan", bad_hex, ok=False)
benchmark = json.loads(run("bench", config, "lan", frames, "100").stdout)
assert benchmark["iterations"] == 100 and benchmark["emitted"] == 100 and benchmark["warmup"] == 100
run("bench", config, "lan", frames, "1000001", ok=False)
mixed = cases / "mixed.hex"
second_frame = bytearray(frame)
second_frame[34:36] = struct.pack("!H", 12346)
mixed.write_text(frame.hex() + "\n" + second_frame.hex() + "\n")
indexed = project / "examples" / "indexed-appliance.conf"
assert run("replay", config, "lan", mixed).stdout == run("replay", indexed, "lan", mixed).stdout

warm = json.loads(run("bench-mix", config, "lan", mixed, "12", "warm").stdout)
assert warm["iterations"] == warm["emitted"] == warm["tracked_udp"] == 12
assert warm["warmup"] == warm["corpus_frames"] == warm["flow_count"] == 2 and warm["new_udp_mappings"] == warm["dropped"] == 0
cold = json.loads(run("bench-mix", config, "lan", mixed, "2", "cold").stdout)
assert cold["warmup"] == 0 and cold["new_udp_mappings"] == cold["emitted"] == 2
assert cold["input_bytes"] == len(frame) * 2 and cold["flow_count"] == 2
run("bench-mix", config, "lan", mixed, "3", "cold", ok=False)
run("bench-mix", config, "lan", mixed, "2", "unknown", ok=False)
run("bench-mix", dynamic_file, "lan", mixed, "2", "warm", ok=False)
if sys.platform != "linux":
    assert "requires Linux" in run("run", config, ok=False).stderr
    assert "requires Linux" in run("run-workers", config, "2", ok=False).stderr
for invalid_workers in ("0", "3", "-1", "02"):
    assert "workers must be 1 or 2" in run("run-workers", config, invalid_workers, ok=False).stderr
print("PASS: CLI validation, bounded input, replay, independent checksum checks and benchmark interface")
