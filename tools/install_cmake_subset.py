#!/usr/bin/env python3
"""Bounded optional CMake/Make dependency install, existing Linux builder only."""
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys

ALLOWED = {'cmake', 'cmake-data', 'make', 'libjsoncpp25', 'librhash0', 'libuv1t64',
           'libarchive13t64', 'libcurl4t64'}


def main():
    if platform.system() != 'Linux' or os.environ.get('NETFORGE_CAPPED_BUILDER') != '1':
        raise SystemExit('Only the existing capped Linux builder is supported')
    if shutil.which('cmake') and shutil.which('make'):
        print('PASS: existing CMake/Make available; no installation')
        return
    log = pathlib.Path(sys.argv[1])
    simulation = subprocess.run(['apt-get', '--simulate', '--no-install-recommends', '--no-upgrade',
                                 'install', 'cmake', 'make'], check=True, capture_output=True,
                                text=True, timeout=25).stdout
    if re.search(r'^(Remv |Inst \S+ \[)', simulation, re.MULTILINE):
        raise SystemExit('STOP: removal or existing-package upgrade proposed')
    planned = re.findall(r'^Inst (\S+) \((\S+)', simulation, re.MULTILINE)
    if not planned or len(planned) > 8 or any(n.split(':')[0] not in ALLOWED for n, _ in planned):
        raise SystemExit('STOP: unsupported CMake dependency plan; no installation')
    selected = [name + '=' + version for name, version in planned]
    metadata = subprocess.run(['apt-cache', 'show', *selected], check=True, capture_output=True,
                              text=True, timeout=20).stdout
    records = {}
    for paragraph in metadata.split('\n\n'):
        fields = dict(line.split(': ', 1) for line in paragraph.splitlines()
                      if ': ' in line and not line.startswith(' '))
        records[(fields.get('Package'), fields.get('Version'))] = fields
    installed = download = 0
    for name, version in planned:
        item = records.get((name.split(':')[0], version), {})
        if not item.get('Size', '').isdigit() or not item.get('Installed-Size', '').isdigit():
            raise SystemExit('STOP: missing exact-version size metadata')
        installed += int(item['Installed-Size']) * 1024
        download += int(item['Size'])
    print(json.dumps({'packages': len(planned), 'gross_installed_MiB': round(installed / 1048576, 2),
                      'download_MiB': round(download / 1048576, 2)}, indent=2), flush=True)
    if installed > 128 * 1048576 or download > 64 * 1048576:
        raise SystemExit('STOP: builder tool budget exceeded')
    if shutil.disk_usage('/tmp').free < installed + download + 512 * 1048576:
        raise SystemExit('STOP: insufficient builder headroom')
    with log.open('x') as output:
        subprocess.run(['apt-get', '-y', '--no-install-recommends', '--no-upgrade', '--no-remove',
                        '-o', 'Acquire::Retries=1', '-o', 'Acquire::http::Timeout=20',
                        '-o', 'Acquire::https::Timeout=20', 'install', *selected],
                       env={**os.environ, 'DEBIAN_FRONTEND': 'noninteractive'},
                       stdout=output, stderr=subprocess.STDOUT, check=True, timeout=180)
    print('PASS: bounded CMake/Make install in builder; log=' + str(log), flush=True)


if __name__ == '__main__':
    main()
