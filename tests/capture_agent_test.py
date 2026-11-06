"""Linux owned-child, idempotency, job/log/input bounds and cancellation contract."""
import json
import os
from pathlib import Path
import sys
import tempfile
import time
sys.dont_write_bytecode=True
compiler,project=sys.argv[1:]
sys.path.insert(0,str(Path(project)/'control'))
from capture_agent import CaptureAgent

def wait(agent):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        if agent.thread is not None and not agent.thread.is_alive():return agent.status()['jobs'][-1]
        time.sleep(.01)
    raise AssertionError('child did not reach bounded terminal state')

def rejects(call):
    try:call()
    except (RuntimeError,ValueError):return
    raise AssertionError('invalid request accepted')

with tempfile.TemporaryDirectory(prefix='netforge-capture-test-') as name:
    root=Path(name);engine=root/'engine';input_file=root/'input.pcap';input_file.write_bytes(b'fixture')
    engine.write_text('#!'+sys.executable+'\n'+'''import json,signal,sys,time
from pathlib import Path
if sys.argv[-1]=='--check':print(json.dumps({'valid':True,'generation':1}));raise SystemExit
config=Path(sys.argv[1]).read_text()
if '# flood' in config:
 print('x'*70000,flush=True);time.sleep(10)
if '# hang' in config:time.sleep(10)
Path(sys.argv[5]).write_bytes(Path(sys.argv[4]).read_bytes())
Path(sys.argv[6]).write_bytes(b'')
print(json.dumps(dict(mode='pcap',complete=True,received=1,routed=1,dropped=0,punted=0,sent=1,unsent=0)),flush=True)
''');engine.chmod(0o755)
    config=root/'config.conf';config.write_text((Path(project)/'examples/xdp-veth.conf').read_text())
    agent=CaptureAgent(engine,compiler,config,input_file,root/'normal')
    try:
        assert (root/'normal/token').stat().st_mode&0o077==0
        first=agent.start(1,'request-1');assert agent.start(1,'request-1')['id']==first['id']
        assert wait(agent)['state']=='completed' and len(agent.status()['jobs'])==1
        assert agent.start(1,'request-1')['id']==1
        rejects(lambda:agent.start(True,'bad'));rejects(lambda:agent.start(1,'bad\nheader'))
        rejects(lambda:agent.start(2,'stale'));rejects(lambda:agent.stop(999))
        for i in range(2,5):agent.start(1,'request-'+str(i));assert wait(agent)['state']=='completed'
        rejects(lambda:agent.start(1,'request-5'))
    finally:agent.close()
    for mode in ('hang','flood'):
        config.write_text((Path(project)/'examples/xdp-veth.conf').read_text()+'\n# '+mode+'\n')
        agent=CaptureAgent(engine,compiler,config,input_file,root/mode)
        try:
            agent.start(1,'request')
            if mode=='hang':
                rejects(lambda:agent.start(1,'concurrent'));agent.stop(1)
            job=wait(agent)
            assert job['state']==('cancelled' if mode=='hang' else 'failed'),job
            assert agent.child.poll() is not None
            assert (root/mode/'job-1/runtime.log').stat().st_size<=65536
        finally:agent.close()
    oversized=root/'big';oversized.write_bytes(b'x'*(1024*1024+1))
    rejects(lambda:CaptureAgent(engine,compiler,config,oversized,root/'oversized'))
print('PASS: capture supervisor pidfd cancellation, idempotency, one-child/four-job/log/input bounds')
