#!/usr/bin/env python3
"""Controller recovery contract with bounded transport doubles."""
import hashlib
import json
import pathlib
import sys
import tempfile
sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / 'control'))
import fleet_journal as fleet

nodes = [{'name': str(i), 'url': 'http://127.0.0.1:' + str(9000+i),
          'expected_generation': 1, 'body': 'version 2\n# node ' + str(i)} for i in range(2)]
state = [{'generation': 1, 'config': 'version 1\n'} for _ in nodes]
posts = []
lost = True

def request(node, method, path, body=None):
    global lost
    i = int(node['name'])
    if method == 'POST':
        data = json.loads(body); assert data['expected_generation'] == state[i]['generation']
        posts.append(i); state[i] = {'generation': 2, 'config': data['config']}
        if lost:
            lost = False
            raise OSError('response lost after apply')
    if path == '/v1/config' and method == 'GET': return dict(state[i])
    return {'generation': state[i]['generation'], 'running': True, 'uncertain': False}

fleet.request = request
with tempfile.TemporaryDirectory() as temp:
    root = pathlib.Path(temp) / 'rollout'
    journal = fleet.Journal(root, nodes)
    try:
        try: fleet.Journal(root, nodes)
        except BlockingIOError: pass
        else: raise AssertionError('concurrent writer accepted')
        try: journal.run(nodes)
        except OSError: pass
        else: raise AssertionError('lost response hidden')
        assert journal.record['states'] == ['intended', 'pending']
    finally: journal.close()
    journal = fleet.Journal(root, nodes)
    try:
        assert journal.run(nodes)['states'] == ['verified', 'verified']
        assert posts == [0, 1]  # No duplicate POST after uncertain response.
        journal.run(nodes); assert posts == [0, 1]
        state[0]['config'] += '# external drift\n'
        try: journal.run(nodes)
        except RuntimeError: pass
        else: raise AssertionError('drift ignored')
    finally: journal.close()
    altered = [dict(n) for n in nodes]; altered[0]['body'] += '# altered\n'
    try: fleet.Journal(root, altered)
    except RuntimeError: pass
    else: raise AssertionError('changed plan accepted')
print('PASS: fleet journal lock, exact-plan identity, lost-response reconciliation, no duplicate POST and drift refusal')

# An intent persisted before a failed connection is deliberately not retried.
state[:] = [{'generation': 1, 'config': 'version 1\n'} for _ in nodes]
posts.clear()
def connection_failure(node, method, path, body=None):
    if method == 'POST':
        posts.append(int(node['name']))
        raise OSError('connection refused before apply')
    if path == '/v1/config': return dict(state[int(node['name'])])
    return {'generation': 1, 'running': True, 'uncertain': False}
fleet.request = connection_failure
with tempfile.TemporaryDirectory() as temp:
    journal = fleet.Journal(pathlib.Path(temp)/'unapplied', nodes)
    try:
        try: journal.run(nodes)
        except OSError: pass
        else: raise AssertionError('connection failure hidden')
        assert journal.record['states'] == ['intended', 'pending']
        try: journal.run(nodes)
        except RuntimeError as error:
            assert 'requires a new operator-reviewed plan' in str(error)
        else: raise AssertionError('unapplied intent retried without corrective plan')
        assert posts == [0] and [item['generation'] for item in state] == [1, 1]
    finally: journal.close()
print('PASS: pre-apply connection failure preserves intent; rejoin refuses blind retry without another POST')
