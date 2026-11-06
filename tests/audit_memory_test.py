"""Audit handler invariants without requiring permission to bind a local port."""
import collections
import hashlib
import io
import json
import pathlib
import sys
import threading
sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(sys.argv[1]).resolve() / 'control'))
from agent import Agent, Handler


class Publisher(Agent):
    def __init__(self):
        self.audit = collections.deque(maxlen=64); self.audit_sequence = 0
        self.condition = threading.Condition(); self.generation = 1; self.uncertain = False
        self.token = 'private-test-token-0123456789'

    def publish(self, expected, data):
        if type(expected) is not int or expected != self.generation: raise ValueError('conflict')
        self.generation += 1
        return {'generation': self.generation, 'running': True, 'uncertain': False}


class Request(Handler):
    def __init__(self, agent, body, request_id='memory-check', disconnect=False):
        data = json.dumps(body).encode()
        self.headers = {'Authorization': 'Bearer ' + agent.token, 'Content-Length': str(len(data)),
                        'X-Netforge-Request-ID': request_id}
        self.server = type('Server', (), {'agent': agent})()
        self.path = '/v1/config'; self.rfile = io.BytesIO(data); self.disconnect = disconnect

    def reply(self, status, body, content_type='application/json'):
        self.status = status; self.body = body
        if self.disconnect: raise BrokenPipeError('controlled response disconnect')


publisher = Publisher()
request = Request(publisher, {'expected_generation': 1, 'config': 'sensitive-config'})
request.do_POST()
assert request.status == 200 and request.body['request_id'] == 'memory-check'
assert publisher.audit[-1]['candidate_sha256'] == hashlib.sha256(b'sensitive-config').hexdigest()
for _ in range(70):
    request = Request(publisher, {'expected_generation': 1, 'config': 'sensitive-config'})
    request.do_POST(); assert request.status == 409
snapshot = publisher.publication_audit()
assert len(snapshot['events']) == 64 and snapshot['evicted'] == 7 and snapshot['last_sequence'] == 71
assert publisher.token not in json.dumps(snapshot) and 'sensitive-config' not in json.dumps(snapshot)
request = Request(publisher, {'expected_generation': 2, 'config': 'next'}, disconnect=True)
try: request.do_POST()
except BrokenPipeError: pass
else: raise AssertionError('disconnect injection failed')
assert publisher.audit_sequence == 72 and publisher.audit[-1]['outcome'] == 'applied'
assert publisher.generation == 3  # A lost response never rolls back applied state.
request = Request(publisher, {}, request_id='bad id'); request.do_POST()
assert request.status == 400 and publisher.audit[-1]['request_id'] != 'bad id'
print('PASS: in-memory audit bounds/hash/privacy and single applied outcome after response disconnect')
