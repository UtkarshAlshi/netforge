#!/usr/bin/env python3
"""Refresh/check the explicit source package manifest; excludes generated state."""
import argparse
import hashlib
from pathlib import Path

ROOT_FILES = {'README.md', 'CHANGELOG.md', 'SECURITY.md', 'CONTRIBUTING.md', 'LICENSE',
              'VERSION', 'CMakeLists.txt', '.gitignore', 'build.sh', 'test.sh'}
ROOTS = ('.github', 'src', 'include', 'control', 'backends', 'benchmarks', 'tests',
         'tools', 'examples', 'docs', 'results', 'lab')
EXTENSIONS = {'.md', '.py', '.cpp', '.hpp', '.h', '.c', '.json', '.yml', '.yaml',
              '.sh', '.txt', '.conf', '.svg', '.html'}


def expected(root):
    names = set(ROOT_FILES)
    for directory in ROOTS:
        for path in (root / directory).rglob('*'):
            if '__pycache__' in path.parts or '.git' in path.parts:
                continue
            if path.is_file() and path.suffix in EXTENSIONS:
                names.add(path.relative_to(root).as_posix())
    if len(names) > 1023:
        raise ValueError('source manifest exceeds file budget')
    records, total = [], 0
    for name in sorted(names):
        path = root / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('source must be regular files: ' + name)
        data = path.read_bytes(); total += len(data)
        if len(data) > 2 * 1024**2 or total > 16 * 1024**2:
            raise ValueError('source byte budget exceeded')
        records.append(hashlib.sha256(data).hexdigest() + '  ' + name + '\n')
    return ''.join(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--check', action='store_true')
    args = parser.parse_args(); root = Path(__file__).resolve().parents[1]
    content = expected(root); path = root / 'SOURCE-MANIFEST.sha256'
    if args.check:
        if path.read_text() != content:
            raise SystemExit('Manifest is stale: run python3 tools/update_manifest.py')
        print('PASS: source manifest matches authored files')
    else:
        path.write_text(content); print('UPDATED source manifest files=' + str(len(content.splitlines())))


if __name__ == '__main__':
    main()
