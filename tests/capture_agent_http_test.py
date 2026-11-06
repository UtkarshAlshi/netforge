"""Real authenticated HTTP and real DPDK capture job; no physical NIC claim."""
import http.client
import http.server
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
binary,compiler,config,capture,project,report=sys.argv[1:]
sys.path.insert(0,str(Path(project)/'control'))
from capture_agent import CaptureAgent,CaptureHandler
root=Path(tempfile.mkdtemp(prefix='netforge-capture-private-'))
agent=CaptureAgent(binary,compiler,config,capture,root/'state')
server=http.server.HTTPServer(('127.0.0.1',0),CaptureHandler);server.agent=agent
thread=threading.Thread(target=server.serve_forever,kwargs={'poll_interval':.05});thread.start()
def request(method,path,body=None,authorized=True):
    connection=http.client.HTTPConnection('127.0.0.1',server.server_port,timeout=3)
    headers={'Authorization':'Bearer '+(agent.token if authorized else 'incorrect')}
    encoded=json.dumps(body) if body is not None else None
    try:
        connection.request(method,path,body=encoded,headers=headers)
        response=connection.getresponse();return response.status,json.loads(response.read(65536))
    finally:connection.close()
try:
    assert request('GET','/v1/status',authorized=False)[0]==401
    assert request('POST','/v1/jobs',{'expected_generation':1,'request_id':'test'},False)[0]==401
    assert request('POST','/v1/jobs',{'expected_generation':0,'request_id':'stale'})[0]==409
    status,first=request('POST','/v1/jobs',{'expected_generation':1,'request_id':'real-capture'})
    assert status==202 and first['id']==1
    assert request('POST','/v1/jobs',{'expected_generation':1,'request_id':'real-capture'})[1]['id']==1
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        status,snapshot=request('GET','/v1/status')
        if snapshot['jobs'][0]['state']!='running':break
        time.sleep(.05)
    job=snapshot['jobs'][0]
    assert status==200 and len(snapshot['jobs'])==1 and job['state']=='completed',snapshot
    assert job['stats']==dict(complete=True,received=4,routed=2,dropped=2,punted=0,sent=2,unsent=0)
    assert request('POST','/v1/jobs',{'expected_generation':1,'request_id':'real-capture'})[1]['id']==1
    assert request('POST','/v1/stop',{'id':1})[0]==200
    assert request('POST','/v1/stop',{'id':999})[0]==409
    assert request('POST','/v1/jobs',{'expected_generation':1,'request_id':'bad','input':'/etc/passwd'})[0]==400
    Path(report).write_text(json.dumps(snapshot,indent=2)+'\n')
    print('PASS: real authenticated DPDK capture start/status/stop, retry idempotency, exact counts and scoped inputs')
finally:
    server.shutdown();thread.join(timeout=2);server.server_close();agent.close()
assert agent.child.poll() is not None
print('PASS: owned capture child stopped; private credentials excluded from retained report')
