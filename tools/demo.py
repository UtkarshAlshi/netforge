#!/usr/bin/env python3
"""Build one portable binary and create a verified browser-viewable packet demo."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path, help='new directory, e.g. ../netforge-demo')
    parser.add_argument('--binary', type=Path, help='reuse an already built reference executable')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    if output == project or project in output.parents:
        parser.error('put demo output outside the source checkout')
    output.mkdir(mode=0o700)
    if args.binary:
        binary = args.binary.resolve(strict=True)
    else:
        build = output / 'build'
        subprocess.run(['cmake', '-S', str(project), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release'], check=True, timeout=60)
        subprocess.run(['cmake', '--build', str(build), '--target', 'netforge', '--parallel', '1'], check=True, timeout=300)
        binary = build / 'netforge'
    subprocess.run([sys.executable, str(project / 'tools/run_demo.py'), str(binary), str(output / 'packets')], check=True, timeout=60)
    print('\nNetForge demo ready: ' + str(output / 'packets/index.html'))
    print('Open that file in a browser. No server, Docker, root access or live network is required.')


if __name__ == '__main__':
    main()
