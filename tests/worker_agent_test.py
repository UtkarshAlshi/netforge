"""Real worker config checker plus controlled ACK runtime for API adapter contract."""
import json
import pathlib
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
binary = pathlib.Path(sys.argv[1]).resolve()
project = pathlib.Path(sys.argv[2]).resolve()
sys.path.insert(0, str(project / 'control'))
from worker_agent import WorkerAgent

with tempfile.TemporaryDirectory(prefix='worker-agent-', dir=binary.parent) as temporary:
    root = pathlib.Path(temporary)
    fake = root / 'engine'
    fake.write_text(f'''#!{sys.executable}
import json, os, pathlib, signal, sys, time
if sys.argv[1] == 'check-workers': os.execv({str(binary)!r}, [{str(binary)!r}] + sys.argv[1:])
assert sys.argv[1] == 'run-workers' and sys.argv[3] in ('1', '2')
p = pathlib.Path(sys.argv[2]); stopped = False
def version(): return int(p.read_text().split('version ')[1].split()[0])
def emit(event): print(json.dumps({{'event': event, 'generation': version()}}), flush=True)
def reload(*_): emit('config_rejected' if 'reject-runtime' in p.read_text() else 'config_published')
def metrics(*_):
 print('netforge_worker_count ' + sys.argv[3], flush=True)
 print('netforge_config_generation ' + str(version()), flush=True)
 print('netforge_rx_oversize_total 0', flush=True)
def stop(*_):
 global stopped
 stopped = True
signal.signal(signal.SIGHUP, reload); signal.signal(signal.SIGUSR1, metrics)
signal.signal(signal.SIGTERM, stop); signal.signal(signal.SIGINT, stop)
emit('started')
while not stopped: time.sleep(.01)
''')
    fake.chmod(0o700)
    initial = project / 'examples/workers-veth.conf'
    original = initial.read_bytes()
    for count in (1, 2):
        agent = WorkerAgent(fake, initial, root / ('state-' + str(count)), count)
        try:
            agent.start(); assert agent.status()['workers'] == count and agent.generation == 1
            update = original.replace(b'version 1', b'version 2')
            assert agent.publish(1, update)['generation'] == 2
            assert f'netforge_worker_count {count}' in agent.snapshot()
            rejected = update.replace(b'version 2', b'version 3') + b'\n# reject-runtime\n'
            try: agent.publish(2, rejected)
            except ValueError: pass
            else: raise AssertionError('runtime rejection hidden')
            assert agent.generation == 2 and agent.active.read_bytes() == update
            unsupported = update.replace(b'version 2', b'version 3') + b'\nneighbor-discovery on\n'
            try: agent.publish(2, unsupported)
            except ValueError: pass
            else: raise AssertionError('worker unsupported config admitted')
            assert agent.active.read_bytes() == update
        finally: agent.close()
    for invalid in (0, 3, True, '2'):
        try: WorkerAgent(fake, initial, root / 'invalid', invalid)
        except ValueError: pass
        else: raise AssertionError('invalid count admitted')
    for count in ('0', '3', '02'):
        result = subprocess.run([str(binary), 'check-workers', str(initial), count], capture_output=True, timeout=5)
        assert result.returncode != 0
print('PASS: worker API adapter real checker, count validation, ACK, metrics, rejection/restoration and unsupported-feature preflight')
