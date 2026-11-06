#!/usr/bin/env python3
"""Counterbalanced bounded offline comparison; verification is inside timed work."""
import json
import pathlib
import platform
import random
import statistics
import subprocess
import sys

binary, project, output = sys.argv[1:]
output = pathlib.Path(output)
output.mkdir(exist_ok=False)

def cpu_stat():
    path = pathlib.Path('/sys/fs/cgroup/cpu.stat')
    return path.read_text()[:4096] if path.is_file() else None

before = cpu_stat()
trials = []
ratios = []
for pair in range(12):
    paired = {}
    for workers in ([1, 2] if pair % 2 == 0 else [2, 1]):
        run = subprocess.run([binary, project, str(workers), '500'], check=True,
                             capture_output=True, text=True, timeout=15)
        if len(run.stdout) > 4096:
            raise RuntimeError('oversize trial result')
        trial = json.loads(run.stdout)
        if trial['verified'] is not True or trial['workers'] != workers or trial['owners'] != workers or trial['packets'] != 8000 or not 0 < trial['seconds'] < 15:
            raise RuntimeError('trial validation failed')
        trial['pair'] = pair
        trial['rate'] = trial['packets'] / trial['seconds']
        paired[workers] = trial
        trials.append(trial)
    ratios.append(paired[2]['rate'] / paired[1]['rate'])
rng = random.Random(771)
bootstrap = sorted(statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(2000))
summary = dict(kind='offline_worker_dispatch_comparison', trials=trials,
               median_paired_two_to_one_rate_ratio=statistics.median(ratios),
               paired_median_bootstrap_95_percentile_interval=[bootstrap[49], bootstrap[1949]],
               environment=dict(kernel=platform.release(), architecture=platform.machine(),
                                cpu_stat_before=before, cpu_stat_after=cpu_stat()),
               limitations=['One producer, batch16,16 warm UDP NAT flows,64-byte frames.',
                            'Timing includes copying, dispatch, futures and byte verification.',
                            'Builder is capped at half a CPU; this is not dedicated-core scaling.',
                            'No socket, NIC throughput, latency or delivery claim.',
                            'Twelve paired samples and bootstrap interval are descriptive, not a universal speedup.'])
(output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
print('PASS:24 counterbalanced verified offline trials; paired two/one median ratio='+str(summary['median_paired_two_to_one_rate_ratio']))
