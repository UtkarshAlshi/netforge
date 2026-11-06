#!/usr/bin/env python3
"""Unpack only bounded regular source files, then verify the exact manifest set."""
import hashlib
from pathlib import Path, PurePosixPath
import sys
import tarfile

archive, destination = map(Path, sys.argv[1:])
if archive.stat().st_size > 16*1024*1024:
    raise SystemExit('archive budget exceeded')
files = {}
total = 0
with tarfile.open(archive, 'r:gz') as source:
    for member in source:
        name = PurePosixPath(member.name)
        if not member.isfile() or name.is_absolute() or '..' in name.parts or len(name.parts)<2 or name.parts[0]!='netforge':
            raise SystemExit('unsafe archive entry')
        relative = str(PurePosixPath(*name.parts[1:]))
        if relative in files or member.size>2*1024*1024 or len(files)>=1024:
            raise SystemExit('duplicate or oversized entry')
        total += member.size
        if total>16*1024*1024:
            raise SystemExit('expanded budget exceeded')
        files[relative] = source.extractfile(member).read(member.size+1)
manifest = files['SOURCE-MANIFEST.sha256'].decode()
expected = {}
for line in manifest.splitlines():
    digest, name = line.split('  ', 1)
    if name in expected:
        raise SystemExit('duplicate manifest path')
    expected[name] = digest
if set(files) != set(expected)|{'SOURCE-MANIFEST.sha256'}:
    raise SystemExit('archive manifest set mismatch')
for name, digest in expected.items():
    if hashlib.sha256(files[name]).hexdigest()!=digest:
        raise SystemExit('archive hash mismatch')
destination.mkdir(exist_ok=False)
for name, data in files.items():
    target = destination/name
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as output:
        output.write(data)
print('PASS: bounded exact manifest extraction files='+str(len(files)))
