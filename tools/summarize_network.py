#!/usr/bin/env python3
"""Summarize retained bounded raw trials; descriptive statistics, no speedup claim."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics


def summarize(data):
    if len(data) > 262144:
        raise ValueError('raw report exceeds256KiB')
    report = json.loads(data)
    if report.get('complete') is not True or len(report.get('trials', [])) != 48:
        raise ValueError('expected48 complete counterbalanced trials')
    rows = []
    trials = report['trials']
    for mode in ('reference', 'dpdk'):
        for size in (64, 128, 512, 1500):
            for burst in (1, 4, 16):
                group = [t for t in trials if (t['mode'], t['frame_bytes'], t['burst']) == (mode, size, burst)]
                if len(group) != 2 or {t['repeat'] for t in group} != {0, 1}:
                    raise ValueError('missing/duplicate condition')
                for trial in group:
                    if (trial['packets_sent'] != 64 or trial['packets_received'] != 64 or trial['missing_packets'] != 0
                            or len(trial['latency_us_raw']) != 64 or trial['elapsed_seconds'] <= 0):
                        raise ValueError('unverified trial')
                rows.append({'mode': mode, 'frame_bytes': size, 'burst': burst, 'repetitions': 2,
                             'median_trial_pps': statistics.median(t['observed_packets_per_second'] for t in group),
                             **{f'median_trial_{p}_us': statistics.median(t[p + '_us'] for t in group) for p in ('p50', 'p95', 'p99')}})
    return {'raw_sha256': hashlib.sha256(data).hexdigest(), 'trial_count': 48, 'verified_packets': 3072,
            'missing_packets': 0, 'conditions': rows, 'claim': 'Descriptive capped virtual-network observations; two trials/condition, no statistical speedup claim.'}


def markdown(summary):
    rows = ['# Descriptive virtual-network observations', '', summary['claim'], '',
            'Each row reports medians of two trial rates and two trial p50/p95/p99 values.',
            'Latency is same-host injection-to-capture, including generator/scheduling overhead.', '',
            '|Mode|Frame bytes|Burst|Median trial packets/s|p50 µs|p95 µs|p99 µs|',
            '|---|---:|---:|---:|---:|---:|---:|']
    for row in summary['conditions']:
        rows.append('|{mode}|{frame_bytes}|{burst}|{median_trial_pps:.0f}|{median_trial_p50_us:.1f}|{median_trial_p95_us:.1f}|{median_trial_p99_us:.1f}|'.format(**row))
    rows += ['', 'Raw SHA256: `' + summary['raw_sha256'] + '`', '', 'This is not a physical-NIC rate, saturation benchmark or confidence-based comparison.']
    return '\n'.join(rows) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path); parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with args.input.open('rb') as source:
        data = source.read(262145)
    result = summarize(data)
    args.output.mkdir(mode=0o700)
    (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    (args.output / 'observations.md').write_text(markdown(result))
    print('PASS:48 trials/3072 packets; bounded descriptive summary saved')


if __name__ == '__main__':
    main()
