"""Generate small synthetic Ethernet fixtures; Python standard library only."""
import argparse
import ipaddress
import pathlib
import struct


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def frame(destination, sport=12345, ttl=64, protocol=17):
    src = ipaddress.IPv4Address("10.77.0.2").packed
    dst = ipaddress.IPv4Address(destination).packed
    if protocol == 17:
        payload = struct.pack("!HHHH", sport, 9000, 11, 0) + b"abc"
        value = checksum(src + dst + b"\0\x11" + struct.pack("!H", len(payload)) + payload) or 65535
        payload = payload[:6] + struct.pack("!H", value) + payload[8:]
    else:
        payload = struct.pack("!BBHHH", 8, 0, 0, 9, 1) + b"abc"
        payload = payload[:2] + struct.pack("!H", checksum(payload)) + payload[4:]
    header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload), 7, 0, ttl, protocol, 0, src, dst)
    header = header[:10] + struct.pack("!H", checksum(header)) + header[12:]
    return (bytes.fromhex("02000000000102000000000a0800") + header + payload).hex()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    fixtures = {
        "router.hex": [frame("10.77.0.130", protocol=1)],
        "nat.hex": [frame("10.77.0.130")],
        "service.hex": [frame("10.77.0.100", sport=10001), frame("10.77.0.100", sport=10002), frame("10.77.0.100", sport=10001)],
        "ttl.hex": [frame("10.77.0.130", ttl=1, protocol=1)],
        "malformed.hex": ["00"],
    }
    for name, frames in fixtures.items():
        with (args.output / name).open("x") as output:
            output.write("\n".join(frames) + "\n")
    print(f"Generated {len(fixtures)} fixtures in {args.output}")
