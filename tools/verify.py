#!/usr/bin/env python3
"""Maintained portable verification entry point; no installs, Docker or NIC changes."""
import argparse
import json
import os
import pathlib
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, help='new dedicated build/report directory')
    parser.add_argument('--sanitizers', action='store_true')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    out = pathlib.Path(args.output).resolve()
    if root == out or root in out.parents:
        raise ValueError('output must be outside the source tree')
    out.mkdir(mode=0o700)  # Exclusive; never overwrite or delete earlier work.
    started = time.monotonic()
    commands = [
        ['cmake', '-S', str(root), '-B', str(out / 'build'), '-DCMAKE_BUILD_TYPE=Debug',
         '-DNETFORGE_SANITIZERS=' + ('ON' if args.sanitizers else 'OFF')],
        ['cmake', '--build', str(out / 'build'), '--parallel', '1'],
        ['ctest', '--test-dir', str(out / 'build'), '--output-on-failure', '--timeout', '60'],
        [os.sys.executable, str(root / 'tests/lab_plan_test.py')],
        [os.sys.executable, str(root / 'tools/package_source.py'), str(out / 'source.tar.gz')],
        [os.sys.executable, str(root / 'tools/unpack_verified.py'), str(out / 'source.tar.gz'), str(out / 'unpacked')],
        [os.sys.executable, str(root / 'tools/run_demo.py'), str(out / 'build/netforge'), str(out / 'demo')],
    ]
    results = []
    try:
        for index, command in enumerate(commands):
            # Logs remain local. No credentials or deployment directories are read.
            with (out / ('step-%02d.log' % index)).open('xb') as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=600)
            results.append({'step': index, 'returncode': result.returncode})
            if result.returncode:
                raise RuntimeError('verification failed; inspect step-%02d.log' % index)
        print('PASS: serial build, tests, lab-plan safety, source archive and offline demo')
    finally:
        (out / 'report.json').write_text(json.dumps({'steps': results, 'elapsed_seconds': time.monotonic() - started,
                                                   'complete': len(results) == len(commands) and all(r['returncode'] == 0 for r in results)}, indent=2) + '\n')


if __name__ == '__main__':
    main()
