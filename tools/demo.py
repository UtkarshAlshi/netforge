#!/usr/bin/env python3
"""Build one portable binary and create a verified browser-viewable packet demo."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import shlex
import subprocess
import sys


def direct_build(project, build, compiler):
    build.mkdir()
    environment = dict(os.environ)
    for key in ('CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH'):
        environment.pop(key, None)
    flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-pthread']
    if platform.system() == 'Darwin':
        flags.append('-stdlib=libc++')
    flags += ['-I', str(project / 'include')]
    objects = []
    for name in ('ethernet', 'ipv4', 'tcp', 'config', 'scheduler', 'appliance', 'workers', 'main', 'linux_io'):
        target = build / (name + '.o')
        subprocess.run([*compiler, *flags, '-c', str(project / 'src' / (name + '.cpp')), '-o', str(target)],
                       env=environment, check=True, timeout=120)
        objects.append(str(target))
    subprocess.run([*compiler, *flags, *objects, '-o', str(build / 'netforge')],
                   env=environment, check=True, timeout=60)
    return build / 'netforge'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path, help='new directory, e.g. ../netforge-demo')
    parser.add_argument('--binary', type=Path, help='reuse an already built reference executable')
    parser.add_argument('--build-system', choices=('auto', 'cmake', 'direct'), default='auto',
                        help='auto uses CMake if present, otherwise the existing C++20 compiler')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    if output == project or project in output.parents:
        parser.error('put demo output outside the source checkout')
    if output.exists():
        parser.error('output already exists; choose a new path, e.g. ../netforge-demo-2 (nothing was removed)')
    if args.binary:
        try:
            binary = args.binary.resolve(strict=True)
        except OSError:
            parser.error('--binary must point to an existing executable')
        if not binary.is_file() or not os.access(binary, os.X_OK):
            parser.error('--binary must point to an executable regular file')
    else:
        cmake = shutil.which('cmake')
        selected = 'cmake' if args.build_system == 'auto' and cmake else args.build_system
        if selected == 'auto':
            selected = 'direct'
        if selected == 'cmake' and not cmake:
            parser.error('CMake is not installed. Use --build-system direct with your existing C++20 compiler.')
        if selected == 'direct':
            try:
                compiler = shlex.split(os.environ.get('CXX', 'c++'))
            except ValueError:
                parser.error('CXX contains invalid quoting')
            executable = shutil.which(compiler[0]) if compiler else None
            if not executable:
                parser.error('No C++ compiler found. Install your platform development tools before building.')
            compiler[0] = executable
    # All dependency/path preflight happens before creating output. Never leave an
    # empty destination merely because CMake/compiler/binary is missing.
    output.mkdir(mode=0o700)
    if not args.binary:
        build = output / 'build'
        if selected == 'cmake':
            subprocess.run([cmake, '-S', str(project), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release'], check=True, timeout=60)
            subprocess.run([cmake, '--build', str(build), '--target', 'netforge', '--parallel', '1'], check=True, timeout=300)
            binary = build / 'netforge'
        else:
            print('Building serially with the existing C++20 compiler; CMake is not required.', flush=True)
            binary = direct_build(project, build, compiler)
    subprocess.run([sys.executable, str(project / 'tools/run_demo.py'), str(binary), str(output / 'packets')], check=True, timeout=60)
    print('\nNetForge demo ready: ' + str(output / 'packets/index.html'))
    print('Open that file in a browser. No server, Docker, root access or live network is required.')


if __name__ == '__main__':
    main()
