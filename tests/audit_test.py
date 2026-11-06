"""Real HTTP audit contract; controlled publisher, no raw sockets or installation."""
import collections
import hashlib
import http.client
import http.server
import json
import pathlib
import sys
import threading
sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(sys.argv[1]).resolve() / 'control'))
from agent import Agent, Handler
import audit_memory_test  # Same invariants also run without network access.


class Publisher(Agent):
    def __init__(self):
        self.audit = collections.deque(maxlen=64); self.audit_sequence = 0
        self.condition = threading.Condition(); self.generation = 1; self.uncertain = False
        self.token = 'private-token-do-not-audit-0123456789'

    def publish(self, expected, data):
        if type(expected) is not int or expected != self.generation: raise ValueError('conflict')
        if data == b'uncertain': self.uncertain = True; raise RuntimeError('ack lost')
        if data != b'accept': raise ValueError('rejected')
        self.generation += 1
        return {'generation': self.generation, 'running': True, 'uncertain': self.uncertain}


agent = Publisher()
server = http.server.HTTPServer(('127.0.0.1', 0), Handler); server.agent = agent
thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': .01}); thread.start()


def request(method, path, body=None, token=None, request_id=None):
    connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=3)
    try:
        headers = {'Authorization': 'Bearer ' + (agent.token if token is None else token)}
        if request_id is not None: headers['X-Netforge-Request-ID'] = request_id
        connection.request(method, path, json.dumps(body) if body is not None else None, headers)
        response = connection.getresponse(); raw = response.read(65537)
        assert len(raw) <= 65536
        return response.status, json.loads(raw), response.getheader('X-Netforge-Request-ID')
    finally: connection.close()


try:
    data = {'expected_generation': 1, 'config': 'accept'}
    assert request('POST', '/v1/config', data, token='bad')[0] == 401
    assert request('GET', '/v1/audit', token='bad')[0] == 401
    assert agent.audit_sequence == 0
    code, body, request_id = request('POST', '/v1/config', data, request_id='rollout-node-1')
    assert code == 200 and body['request_id'] == request_id == 'rollout-node-1'
    code, audit, _ = request('GET', '/v1/audit'); event = audit['events'][0]
    assert code == 200 and event['sequence'] == 1 and event['outcome'] == 'applied'
    assert event['expected_generation'] == 1 and event['applied_generation'] == 2
    assert event['candidate_sha256'] == hashlib.sha256(b'accept').hexdigest()
    assert request('POST', '/v1/config', data, request_id='unsafe id')[0] == 400
    assert agent.audit[-1]['request_id'] != 'unsafe id'
    assert request('POST', '/v1/config', [], request_id='malformed')[0] == 409
    assert agent.audit[-1]['candidate_sha256'] is None
    for _ in range(70):
        assert request('POST', '/v1/config', {'expected_generation': 1, 'config': 'sensitive-body'}, request_id='repeat')[0] == 409
    audit = request('GET', '/v1/audit')[1]
    assert len(audit['events']) == audit['capacity'] == 64
    assert audit['evicted'] == audit['last_sequence'] - 64 and audit['persistent'] is False
    sequence = [e['sequence'] for e in audit['events']]
    assert sequence == list(range(audit['last_sequence'] - 63, audit['last_sequence'] + 1))
    encoded = json.dumps(audit)
    assert agent.token not in encoded and 'sensitive-body' not in encoded
    assert request('POST', '/v1/config', {'expected_generation': 2, 'config': 'uncertain'})[0] == 503
    assert agent.audit[-1]['outcome'] == 'uncertain' and agent.audit[-1]['uncertain'] is True
finally:
    server.shutdown(); server.server_close(); thread.join(timeout=2)
print('PASS: authenticated bounded HTTP audit, correlation/header, hashes, outcomes, eviction, uncertainty and secret/body exclusion')
