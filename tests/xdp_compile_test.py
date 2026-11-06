"""Offline fail-closed configuration/compiler acceptance; creates no interfaces."""
import pathlib
import struct
import subprocess
import sys
import tempfile

binary, root = sys.argv[1], pathlib.Path(sys.argv[2])
base = (root / "examples/xdp-veth.conf").read_text()
with tempfile.TemporaryDirectory(prefix="netforge-xdp-compile-") as directory:
    work = pathlib.Path(directory)
    config, output = work / "input.conf", work / "policy.bin"

    def run(text, bindings=("lan=3", "wan=4"), accepted=False):
        config.write_text(text)
        output.write_bytes(b"unchanged")
        result = subprocess.run([binary, str(config), "lan", str(output), *bindings],
                                capture_output=True, text=True, timeout=5)
        assert (result.returncode == 0) == accepted, result.stderr
        if not accepted:
            assert output.read_bytes() == b"unchanged", "rejection changed output"
        return output.read_bytes()

    policy = run(base, accepted=True)
    assert len(policy) == 1044
    assert struct.unpack_from("=III", policy) == (1, 1, 0)
    assert policy[12:18] == bytes.fromhex("028800000002")
    assert struct.unpack_from("=IIIHHBBBB", policy, 20) == (
        3, 0x0A580000, 0x0A590000, 0, 9000, 24, 24, 17, 1)
    assert struct.unpack_from("=IIHB", policy, 660) == (0x0A590002, 4, 1500, 32)
    # Interchange bytes have an independent big-endian field oracle, no ABI padding.
    portable = run(base, bindings=("lan=3", "wan=4", "--portable"), accepted=True)
    expected_wire = (b"NFPOL\x00\x00\x01" + struct.pack(">III", 1, 1, 0) + bytes.fromhex("028800000002") +
                     struct.pack(">IIIHHBBBB", 3, 0x0A580000, 0x0A590000, 0, 9000, 24, 24, 17, 1) +
                     struct.pack(">IIHB", 0x0A590002, 4, 1500, 32) +
                     bytes.fromhex("028900000001028900000002"))
    assert portable == expected_wire and len(portable) == 69
    wire, decoded = work / "wire.bin", work / "decoded.bin"
    wire.write_bytes(portable)
    subprocess.run([binary, "--decode-policy", str(wire), str(decoded)], check=True, capture_output=True, timeout=5)
    assert decoded.read_bytes() == policy
    malformed = [portable[:length] for length in range(len(portable))]
    malformed += [portable + b"x", portable[:7] + b"\x02" + portable[8:], b"x"*1035]
    for offset, value in [(8, 255), (20+6+16, 33), (20+6+19, 2)]:
        mutation = bytearray(portable); mutation[offset] = value; malformed.append(bytes(mutation))
    for bad in malformed:
        wire.write_bytes(bad); decoded.write_bytes(b"unchanged")
        result = subprocess.run([binary, "--decode-policy", str(wire), str(decoded)], capture_output=True, timeout=5)
        assert result.returncode != 0 and decoded.read_bytes() == b"unchanged"
    for extra in ("snat wan 10.89.0.1 40000 40010\n", "tcp-stateful on\n",
                  "neighbor-discovery on\n", "flow-index on\n",
                  "service lan 10.88.0.100 9000 10.89.0.2:9000\n"):
        run(base + extra)
    run(base.replace("default deny", "default allow"))
    run(base.replace("lan udp", "lan tcp"))
    run(base.replace("route 10.89.0.2/32", "route 10.89.0.0/24"))
    run(base.replace("neighbor wan 10.89.0.2 02:89:00:00:00:02", ""))
    run(base, ("lan=3",))
    run(base, ("lan=3", "wan=3"))
    run(base, ("lan=0", "wan=4"))
    run(base, ("lan=@wrong-device", "wan=4"))
    run(base, ("lan=@nf-in", "wan=@nf-out"))  # These private devices do not exist in the builder.
    run(base + ("acl allow lan udp 0.0.0.0/0 0.0.0.0/0 * 9000\n" * 32))
    # Gateway routes compile only with an explicit static next-hop neighbor.
    gateway = base.replace("10.89.0.2/32 wan direct", "10.90.0.0/24 wan 10.89.0.2")
    assert len(run(gateway, accepted=True)) == 1044
    run(gateway.replace("route 10.90.0.0/24", "route 0.0.0.0/0"))
print("PASS: XDP config ABI, explicit bindings, static next hops and unsupported-feature rejection")
