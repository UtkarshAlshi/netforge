#!/usr/bin/env python3
"""Verify the source manifest and create an exclusive, bounded source-only archive."""
import argparse
import gzip
import hashlib
import io
import tarfile
from pathlib import Path, PurePosixPath


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='new .tar.gz file; overwrite refused')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = (root/'SOURCE-MANIFEST.sha256').read_bytes()
    entries = []
    seen = set()
    total = 0
    for line in manifest.decode().splitlines():
        digest, name = line.split('  ', 1)
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or name in seen:
            raise SystemExit('STOP: unsafe or duplicate manifest path')
        seen.add(name)
        path = root/name
        if path.is_symlink() or root not in path.resolve().parents or not path.is_file():
            raise SystemExit('STOP: nonregular or escaped source path')
        if path.stat().st_size > 2*1024*1024:
            raise SystemExit('STOP: source file exceeds 2 MiB')
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit('STOP: source manifest mismatch: '+name)
        total += len(data)
        if total > 16*1024*1024 or len(entries) >= 1024:
            raise SystemExit('STOP: source package budget exceeded')
        entries.append((name, data))
    entries.append(('SOURCE-MANIFEST.sha256', manifest))
    # Only manifest-listed source is included; no discovery of private runtime files.
    with args.output.open('xb') as raw:
        with gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0) as zipped:
            with tarfile.open(fileobj=zipped, mode='w') as archive:
                for name, data in sorted(entries):
                    info = tarfile.TarInfo('netforge/'+name)
                    info.size = len(data)
                    info.mode = 0o644
                    info.mtime = 0
                    archive.addfile(info, io.BytesIO(data))
    print('SAVED', args.output, 'files='+str(len(entries)), 'source_bytes='+str(total))
    print('SHA256', hashlib.sha256(args.output.read_bytes()).hexdigest())

if __name__ == '__main__':
    main()
