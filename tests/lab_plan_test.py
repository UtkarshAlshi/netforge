"""Plan must remain read-only and reject namespace/subnet collisions."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
spec=importlib.util.spec_from_file_location('lab',Path(__file__).resolve().parents[1]/'tools/lab.py')
lab=importlib.util.module_from_spec(spec);spec.loader.exec_module(lab)
for conflict in ('none','container','network'):
    calls=[]
    def docker(*args):
        calls.append(args)
        if args[0]=='info':return json.dumps(dict(OSType='linux',NCPU=2,MemTotal=2069843968))
        if args[:2]==('image','inspect'):return json.dumps([dict(Os='linux',Id='sha256:fixed')])
        if args[0]=='ps':return 'netforge-repro-a\n' if conflict=='container' else ''
        if args[:2]==('network','ls'):return 'existing\n'
        if args[:2]==('network','inspect'):return json.dumps([dict(Name='other',IPAM=dict(Config=[dict(Subnet='10.79.0.0/24' if conflict=='network' else '10.77.0.0/24')]))])
        raise AssertionError('unexpected/mutating call '+str(args))
    lab.docker=docker
    sys.argv=['lab.py','plan','--name','netforge-repro']
    try:
        with contextlib.redirect_stdout(io.StringIO()):lab.main()
    except SystemExit:
        assert conflict!='none'
    else:assert conflict=='none'
    assert all(args[0] in ('info','image','ps','network') for args in calls)
print('PASS: lab plan read-only; container and subnet collision refusal')
