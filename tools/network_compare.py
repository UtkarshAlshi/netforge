#!/usr/bin/env python3
"""Bounded same-policy private-veth reference/DPDK experiment; no NIC setup."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import time


def checksum(data):
    total = sum(struct.unpack('!' + str(len(data) // 2) + 'H', data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def frame(size, token, forwarded=False):
    payload = struct.pack('!I', token) + bytes((i % 251 for i in range(size - 46)))
    udp = struct.pack('!HHHH', 12000, 9000, size - 34, 0) + payload
    ip = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, size - 14, 7, 0, 63 if forwarded else 64,
                               17, 0, bytes([10, 88, 0, 2]), bytes([10, 89, 0, 2])))
    struct.pack_into('!H', ip, 10, checksum(bytes(ip)))
    macs = '0289000000020289000000010800' if forwarded else '0288000000020288000000010800'
    return bytes.fromhex(macs) + bytes(ip) + udp


def quantile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def trial(command, mode, size, burst, index, output):
    source = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800))
    sink = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800))
    source.bind(('nf-source', 0)); sink.bind(('nf-sink', 0)); sink.settimeout(2)
    packets, timings, received, started_at = 64, [], set(), {}
    # Both modes expose their start event before input is injected.
    with (output / ('runtime-%02d.log' % index)).open('xb') as log:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=log if mode == 'dpdk' else subprocess.STDOUT)
        try:
            assert select.select([process.stdout], [], [], 15)[0], 'start deadline'
            event = json.loads(process.stdout.readline())
            assert event.get('event') == ('dpdk_live_started' if mode == 'dpdk' else 'started'), event
            start = time.monotonic_ns()
            for first in range(0, packets, burst):
                for token in range(first, min(first + burst, packets)):
                    started_at[token] = time.monotonic_ns(); source.send(frame(size, token))
                for _ in range(min(burst, packets - first)):
                    actual = sink.recv(2048); stamp = time.monotonic_ns()
                    assert len(actual) == size
                    token = struct.unpack_from('!I', actual, 42)[0]
                    assert token in started_at and token not in received and actual == frame(size, token, True)
                    received.add(token); timings.append((stamp - started_at[token]) / 1000)
            elapsed = (time.monotonic_ns() - start) / 1e9
            process.send_signal(signal.SIGTERM)
            stdout, _ = process.communicate(timeout=8)
            log.write(stdout)
            assert process.returncode == 0 and len(received) == packets
            return {'mode': mode, 'frame_bytes': size, 'burst': burst, 'packets_sent': packets,
                    'packets_received': len(received), 'missing_packets': packets - len(received),
                    'elapsed_seconds': elapsed, 'observed_packets_per_second': packets / elapsed,
                    'observed_frame_bits_per_second': packets * size * 8 / elapsed,
                    'latency_us_raw': timings, 'p50_us': quantile(timings, .50),
                    'p95_us': quantile(timings, .95), 'p99_us': quantile(timings, .99)}
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=3)
            source.close(); sink.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'dpdk', 'compiler', 'project', 'output'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    if os.environ.get('NETFORGE_HOSTED_DPDK') != '1':
        raise ValueError('requires explicit isolated hosted-container opt-in')
    out = Path(args.output); out.mkdir(mode=0o700)
    config = Path(args.project) / 'examples/xdp-veth.conf'; policy = out / 'policy.bin'
    subprocess.run([args.compiler, str(config), 'lan', str(policy), 'lan=1', 'wan=2'], check=True,
                   capture_output=True, timeout=5)
    commands = {'reference': [args.reference, 'run', str(config)],
                'dpdk': [args.dpdk, str(config), str(policy), 'nf-in', 'nf-out', '20']}
    trials = []
    try:
        for repeat in range(2):
            for size_index, size in enumerate((64, 128, 512, 1500)):
                for burst in (1, 4, 16):
                    order = ('reference', 'dpdk') if (repeat + size_index) % 2 == 0 else ('dpdk', 'reference')
                    for mode in order:
                        result = trial(commands[mode], mode, size, burst, len(trials), out)
                        result['repeat'] = repeat; trials.append(result)
    finally:
        report = {'schema': 1, 'complete': len(trials) == 48, 'trials': trials,
                  'config_sha256': hashlib.sha256(config.read_bytes()).hexdigest(),
                  'binaries_sha256': {mode: hashlib.sha256(Path(command[0]).read_bytes()).hexdigest() for mode, command in commands.items()},
                  'boundary': 'same-host private-veth user-space injection to capture; 0.5CPU/512MiB shared cap; includes generator/scheduler/capture overhead',
                  'not_claimed': ['NIC wire rate', 'maximum throughput', 'hardware latency', 'XDP/Linux-kernel comparison', 'statistical speedup']}
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS:48 counterbalanced byte-verified reference/DPDK virtual-network trials; raw latency and offered/observed counts retained')


if __name__ == '__main__':
    main()
