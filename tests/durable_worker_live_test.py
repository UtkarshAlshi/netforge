"""Hosted private-veth HTTP/crash recovery with real two-owner AF_PACKET traffic."""
import http.client
import json
import os
from pathlib import Path
import select
import signal
import time
import subprocess
import sys
import tempfile
binary,probe,project,report=sys.argv[1:]
project=Path(project);sys.path.insert(0,str(project/'control'))
from durable_agent import DurableAgent,identity
from durable_worker_agent import DurableWorkerAgent

def ip(*args):subprocess.run(['ip',*args],check=True)
ip('link','add','nf-source','type','veth','peer','name','nf-in')
ip('link','add','nf-out','type','veth','peer','name','nf-sink')
for device,mac in [('nf-source','02:88:00:00:00:01'),('nf-in','02:88:00:00:00:02'),('nf-out','02:89:00:00:00:01'),('nf-sink','02:89:00:00:00:02')]:
    ip('link','set',device,'address',mac);ip('link','set',device,'up')
root=Path(tempfile.mkdtemp(prefix='netforge-durable-worker-private-'));state=root/'state'
config=project/'examples/workers-veth.conf';source=config.read_bytes()
supervisor=subprocess.Popen([sys.executable,str(project/'control/durable_worker_agent.py'),'--binary',binary,'--config',str(config),'--state',str(state),'--workers','2','--port','0'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
agent=None

def refuses(call,word):
    try:call()
    except (RuntimeError,ValueError) as error:
        assert word in str(error),str(error);return
    raise AssertionError('unsafe recovery accepted')

def traffic(*args):
    run=subprocess.run([probe,*args],capture_output=True,text=True,check=True,timeout=8)
    print(run.stdout,end='',flush=True)

try:
    assert select.select([supervisor.stdout],[],[],15)[0],'supervisor startup timeout'
    started=json.loads(supervisor.stdout.readline());assert started['event']=='durable_worker_started'
    token=(state/'token').read_text()
    def request(method,path,body=None):
        connection=http.client.HTTPConnection('127.0.0.1',started['port'],timeout=6)
        try:
            connection.request(method,path,json.dumps(body) if body else None,{'Authorization':'Bearer '+token})
            response=connection.getresponse();value=json.loads(response.read(65536));assert response.status==200,value;return value
        finally:connection.close()
    assert request('GET','/v1/status')['workers']==2
    revision2=source.replace(b'version 1',b'version 2')
    assert request('POST','/v1/config',dict(expected_generation=1,config=revision2.decode()))['generation']==2
    traffic()
    journal=json.loads((state/'journal.json').read_text())['record'];old=journal['child']
    assert old and journal['checkpoint']['generation']==2 and journal['runtime_identity']==dict(mode='workers',workers=2)
    supervisor.kill();supervisor.wait(timeout=3)
    assert identity(old['pid'])==old,'worker child should outlive killed supervisor'
    refuses(lambda:DurableWorkerAgent(binary,None,state,1,resume=True,stop_owned_child=True),'runtime mode/worker count')
    refuses(lambda:DurableAgent(binary,None,state,resume=True,stop_owned_child=True),'runtime mode/worker count')
    refuses(lambda:DurableWorkerAgent(binary,None,state,2,resume=True),'explicit --stop-owned-child')
    assert identity(old['pid'])==old,'refused recovery signalled existing child'
    agent=DurableWorkerAgent(binary,None,state,2,resume=True,stop_owned_child=True)
    agent.start();assert agent.generation==3 and agent.token==token and identity(old['pid'])!=old
    metrics=agent.snapshot()
    assert all(f'netforge_worker_flows{{owner="{owner}"}} 0\n' in metrics for owner in (0,1))
    traffic()
    denied=revision2.replace(b'version 2',b'version 4').replace(b'acl allow lan udp',b'acl deny lan udp')
    assert agent.publish(3,denied)['generation']==4
    traffic('--udp-only');traffic('--deny-new')
    rejected=denied.replace(b'version 4',b'version 5').replace(b'limits 32 ',b'limits 33 ')
    try:agent.publish(4,rejected)
    except ValueError:pass
    else:raise AssertionError('immutable worker state bound changed')
    assert agent.generation==4 and agent.store['checkpoint']['generation']==4
    agent.close();agent=None
    agent=DurableWorkerAgent(binary,None,state,2,resume=True)
    agent.start();assert agent.generation==6 and agent.token==token
    traffic('--deny-new')
    metrics=agent.snapshot()
    assert 'netforge_config_generation 6\n' in metrics and 'netforge_tx_failures_total 0\n' in metrics
    def scalar(snapshot,name):
        return int(next(line.split()[1] for line in snapshot.splitlines() if line.startswith(name+' ')))
    before_drops=scalar(metrics,'netforge_packet_socket_drops_total')
    descriptor=os.pidfd_open(agent.child.pid)
    try:
        signal.pidfd_send_signal(descriptor,signal.SIGSTOP)
        try:traffic('--flood')
        finally:signal.pidfd_send_signal(descriptor,signal.SIGCONT)
    finally:os.close(descriptor)
    time.sleep(.5)
    pressured=agent.snapshot()
    assert scalar(pressured,'netforge_packet_socket_drops_total')>before_drops
    assert scalar(pressured,'netforge_packet_socket_statistics_read_errors_total')==0
    assert scalar(pressured,'netforge_packet_socket_statistics_available_ports')==2
    again=agent.snapshot()
    assert scalar(again,'netforge_packet_socket_drops_total')>=scalar(pressured,'netforge_packet_socket_drops_total')
    print('PASS: actual packet-socket pressure drops are distinct, cumulative across reset-on-read snapshots and available on both ports',flush=True)
    Path(report).write_text(json.dumps(dict(workers=2,crash_checkpoint=2,recovered=3,acknowledged=4,rejected_high_water=5,clean_restart=6,stable_credentials=True,flow_state_persisted=False,exact_child_replaced=True),indent=2)+'\n')
    print('PASS: real durable two-worker HTTP publication, supervisor SIGKILL, pidfd orphan stop, mode/count refusal, cold restart and traffic',flush=True)
finally:
    if agent:agent.close()
    if supervisor.poll() is None:supervisor.terminate();supervisor.wait(timeout=3)
    supervisor.stdout.close()
print('PASS: owned workers shut down; private recovery credentials excluded from artifacts',flush=True)
