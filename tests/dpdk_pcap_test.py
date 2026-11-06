"""Independent PCAP/reference checks against the actual DPDK ring-PMD executable."""
import json
import pathlib
import select
import signal
import struct
import subprocess
import sys

engine, config, policy, reference, directory = sys.argv[1:]
root = pathlib.Path(directory); root.mkdir(exist_ok=False)
HEADER = struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1)


def checksum(data):
    if len(data) & 1: data += b'\0'
    value = sum(struct.unpack('!' + 'H' * (len(data)//2), data))
    while value >> 16: value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def packet(length=64, port=9000, ttl=64, fragment=0):
    payload = bytes((i % 251 for i in range(length - 42)))
    udp = struct.pack('!HHHH', 12345, port, len(payload) + 8, 0) + payload
    ip = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, len(udp) + 20, 123, fragment,
                               ttl, 17, 0, bytes([10, 88, 0, 2]), bytes([10, 89, 0, 2])))
    struct.pack_into('!H', ip, 10, checksum(bytes(ip)))
    return bytes.fromhex('0288000000020288000000010800') + bytes(ip) + udp


def capture(frames):
    return HEADER + b''.join(struct.pack('<IIII', 1700000000, i % 1000000, len(data), len(data)) + data
                             for i, data in enumerate(frames))


def read_capture(path):
    assert path.stat().st_mode & 0o077 == 0
    raw = path.read_bytes(); assert raw[:16] == HEADER[:16] and struct.unpack_from('<I', raw, 20)[0] == 1
    records = []; offset = 24
    while offset < len(raw):
        seconds, micros, length, original = struct.unpack_from('<IIII', raw, offset); offset += 16
        assert length == original and length <= 1514 and offset + length <= len(raw)
        records.append((seconds, micros, raw[offset:offset+length])); offset += length
    assert offset == len(raw)
    return records


def run(path, name, success=True):
    output, punts = root / (name + '-routed.pcap'), root / (name + '-punted.pcap')
    result = subprocess.run([engine, config, policy, '--pcap', str(path), str(output), str(punts)],
                            capture_output=True, text=True, timeout=20)
    (root / (name + '.log')).write_text(result.stdout + result.stderr)
    assert (result.returncode == 0) == success, (name, result.returncode, result.stdout, result.stderr)
    if not success: return
    stats = next(json.loads(line) for line in result.stdout.splitlines() if line.startswith('{"mode"'))
    assert stats['complete'] and stats['unsent'] == 0
    assert 'EAL cleaned up' in result.stdout
    return output, punts, stats


common = []
for length in (64, 128, 512, 1514): common += [packet(length), packet(length, port=9001)]
bad = bytearray(packet()); bad[24] ^= 1
common += [bytes(bad), bytes(10)]
frames = common * 16
punts_expected = [packet(ttl=1), packet(fragment=0x2000), packet()[:12] + b'\x08\x06' + packet()[14:]]
frames += punts_expected
source = root / 'input.pcap'; source.write_bytes(capture(frames))
hexfile = root / 'reference.hex'; hexfile.write_text(''.join(data.hex() + '\n' for data in common * 16))
oracle = subprocess.run([reference, 'replay', config, 'lan', str(hexfile)], check=True,
                        capture_output=True, text=True, timeout=15)
decisions = [json.loads(line) for line in oracle.stdout.splitlines()]
expected = [(1700000000, i, bytes.fromhex(decision['frame'])) for i, decision in enumerate(decisions) if decision['egress']]
output, puntfile, stats = run(source, 'valid')
assert read_capture(output) == expected and len(expected) == 64
assert read_capture(puntfile) == [(1700000000, 160+i, data) for i, data in enumerate(punts_expected)]
assert stats == {'mode': 'pcap', 'complete': True, 'received': 163, 'routed': 64,
                 'dropped': 96, 'punted': 3, 'sent': 64, 'unsent': 0}
print('PASS: real DPDK PCAP163 records,64 byte-exact reference routes at4 sizes,96 drops,3 original punts and timestamps', flush=True)

empty = root / 'empty.pcap'; empty.write_bytes(HEADER)
empty_out, empty_punts, empty_stats = run(empty, 'empty')
assert empty_stats['received'] == 0 and read_capture(empty_out) == read_capture(empty_punts) == []
malformed = [b'\0' * 24, HEADER[:23], HEADER + b'\0' * 15,
             HEADER + struct.pack('<IIII', 1, 0, 64, 64) + bytes(63),
             HEADER + struct.pack('<IIII', 1, 0, 1515, 1515) + bytes(1515),
             HEADER + struct.pack('<IIII', 1, 1000000, 64, 64) + bytes(64),
             HEADER + struct.pack('<IIII', 1, 0, 64, 65) + bytes(64),
             HEADER + struct.pack('<IIII', 1, 0, 0, 0),
             HEADER + (struct.pack('<IIII', 1, 0, 1, 1) + b'\0') * 65537]
for index, raw in enumerate(malformed):
    path = root / ('bad-' + str(index) + '.pcap'); path.write_bytes(raw); run(path, 'bad-' + str(index), False)
oversize = root / 'oversize.pcap'
with oversize.open('wb') as stream:
    stream.write(HEADER); stream.truncate(16 * 1024 * 1024 + 1)
run(oversize, 'oversize', False)
prior = source.read_bytes()
refuse = subprocess.run([engine, config, policy, '--pcap', str(source), str(source), str(root / 'must-not-overwrite.pcap')],
                        capture_output=True, timeout=15)
assert refuse.returncode != 0 and source.read_bytes() == prior
print('PASS: empty capture,10 malformed/budget file cases and existing-output refusal; input unchanged', flush=True)

# A bounded large regular input gives the supervisor a real in-flight stop window.
large = root / 'stop-input.pcap'; large.write_bytes(capture([packet(128)] * 65536))
stop_output, stop_punts = root / 'stop-routed.pcap', root / 'stop-punted.pcap'
process = subprocess.Popen([engine, config, policy, '--pcap', str(large), str(stop_output), str(stop_punts)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
try:
    ready, _, _ = select.select([process.stdout], [], [], 8); assert ready, 'DPDK startup timeout'
    first = process.stdout.readline(); assert 'pcap_started' in first, first
    process.send_signal(signal.SIGTERM)
    stdout, stderr = process.communicate(timeout=8)
    assert process.returncode == 130, (process.returncode, stdout, stderr)
    stopped = next(json.loads(line) for line in stdout.splitlines() if line.startswith('{"mode"'))
    assert not stopped['complete'] and stopped['received'] < 65536
    assert 'EAL cleaned up' in stdout
    assert len(read_capture(stop_output)) == stopped['sent']
    assert read_capture(stop_punts) == []
    (root / 'stop.log').write_text(first + stdout + stderr)
finally:
    if process.poll() is None: process.terminate(); process.wait(timeout=5)
print('PASS: in-flight SIGTERM yields valid partial captures, incomplete status, reclaimed batches and EAL cleanup', flush=True)
(root / 'summary.json').write_text(json.dumps({'status': 'passed', 'main_run': stats,
                                              'reference_routes': len(expected), 'malformed_cases': len(malformed) + 1,
                                              'controlled_stop': stopped}, indent=2) + '\n')
