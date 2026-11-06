"""Worker checker/runtime hooks, mode-bound durable checkpoints and cold restart."""
import json
from pathlib import Path
import sys
import tempfile
sys.dont_write_bytecode=True
binary,project=map(Path,sys.argv[1:]);binary=binary.resolve();project=project.resolve()
sys.path.insert(0,str(project/'control'))
from durable_worker_agent import DurableWorkerAgent
from durable_agent import DurableAgent

def rejected(call):
    try:call()
    except (ValueError,RuntimeError):return
    raise AssertionError('checkpoint runtime substitution accepted')

with tempfile.TemporaryDirectory(prefix='netforge-durable-worker-') as name:
    root=Path(name);fake=root/'engine'
    fake.write_text(f'''#!{sys.executable}
import json,os,signal,sys,time
if sys.argv[1] in ('check','check-workers'):os.execv({str(binary)!r},[{str(binary)!r}]+sys.argv[1:])
assert sys.argv[1]=='run-workers' and sys.argv[3]=='2'
from pathlib import Path
path=Path(sys.argv[2]);stopped=False
def generation():return int(path.read_text().split('version ')[1].split()[0])
def emit(event):print(json.dumps(dict(event=event,generation=generation())),flush=True)
def stop(*_):
 global stopped
 stopped=True
signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGHUP,lambda *_:emit('config_published'))
signal.signal(signal.SIGUSR1,lambda *_:print('netforge_worker_count 2\\nnetforge_rx_oversize_total 0',flush=True))
emit('started')
while not stopped:time.sleep(.01)
''');fake.chmod(0o700)
    config=project/'examples/workers-veth.conf';state=root/'state'
    agent=DurableWorkerAgent(fake,config,state,2)
    try:
        agent.start();token=agent.token
        agent.publish(1,config.read_bytes().replace(b'version 1',b'version 2'))
        assert agent.status()['workers']==2 and not agent.status()['flow_state_persisted']
        record=json.loads((state/'journal.json').read_text())['record']
        assert record['runtime_identity']==dict(mode='workers',workers=2) and record['checkpoint']['generation']==2
    finally:agent.close()
    rejected(lambda:DurableWorkerAgent(fake,None,state,1,resume=True))
    rejected(lambda:DurableAgent(fake,None,state,resume=True))
    recovered=DurableWorkerAgent(fake,None,state,2,resume=True)
    try:
        recovered.start();assert recovered.generation==3 and recovered.token==token
        assert 'netforge_worker_count 2' in recovered.snapshot()
    finally:recovered.close()
print('PASS: durable worker runtime/count identity, checkpoint publication, stable credentials and higher-revision cold restart')
