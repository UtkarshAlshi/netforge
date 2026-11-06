#!/usr/bin/env python3
"""Real TLS sockets: mutual authentication, node binding and journaled rollout."""
import http.client
import json
import os
import pathlib
import ssl
import subprocess
import sys
import tempfile
import threading
sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / 'control'))
from secure_node import SecureServer
from secure_fleet import load_plan, observe
from fleet import request
from fleet_journal import Journal


class Engine:
    token = 'test_private_token_123456789'
    generation = 1
    body = 'version 1\n'
    posts = 0
    def status(self):
        return {'running': True, 'uncertain': False, 'generation': self.generation}
    def configuration(self):
        return {'generation': self.generation, 'config': self.body}
    def publish(self, expected, data):
        if expected != self.generation:
            raise ValueError('stale generation')
        self.generation = int(data.decode().split()[1]); self.body = data.decode(); self.posts += 1
        return self.status()
    def record_publication(self, *_):
        pass


def must_fail(call):
    try:
        call()
    except (OSError, ValueError, RuntimeError, http.client.HTTPException):
        return
    raise AssertionError('unsafe transport accepted')


with tempfile.TemporaryDirectory() as temp:
    root = pathlib.Path(temp)
    def openssl(*args):
        subprocess.run(['openssl', *args], cwd=root, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=15)
    openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=NetForge-test-CA',
            '-keyout', 'ca.key', '-out', 'ca.pem')
    for role in ('server', 'client'):
        openssl('req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=' + role,
                '-keyout', role + '.key', '-out', role + '.csr')
        (root / 'extensions').write_text('subjectAltName=IP:127.0.0.1\nextendedKeyUsage=' +
                                         ('serverAuth' if role == 'server' else 'clientAuth') + '\n')
        openssl('x509', '-req', '-in', role + '.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key',
                '-CAcreateserial', '-days', '1', '-extfile', 'extensions', '-out', role + '.pem')
        (root / (role + '.key')).chmod(0o600)
    live = len(sys.argv) == 3
    if live:
        from agent import Agent
        initial = pathlib.Path(sys.argv[1]) / 'examples/workers-veth.conf'
        engine = Agent(sys.argv[2], initial, root / 'agent')
        candidate = initial.read_text().replace('version 1', 'version 2')
    else:
        engine, candidate = Engine(), 'version 2\n'
    (root / 'token').write_text(engine.token); (root / 'token').chmod(0o600)
    (root / 'next.conf').write_text(candidate)
    server = SecureServer(('127.0.0.1', 0), engine, 'node-a', root / 'server.pem', root / 'server.key', root / 'ca.pem')
    if live:
        engine.start()
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        manifest = [{'name': 'a', 'node_id': 'node-a', 'url': 'https://127.0.0.1:' + str(server.server_port),
                     'ca': 'ca.pem', 'certificate': 'client.pem', 'key': 'client.key', 'token_file': 'token',
                     'config': 'next.conf', 'expected_generation': 1}]
        (root / 'members.json').write_text(json.dumps(manifest))
        nodes = load_plan(root / 'members.json')
        assert request(nodes[0], 'GET', '/v1/status')['node_id'] == 'node-a'
        assert observe(nodes)[0]['reachable']
        wrong = {**nodes[0], 'node_id': 'other'}
        must_fail(lambda: request(wrong, 'GET', '/v1/status'))
        must_fail(lambda: request({**nodes[0], 'token': 'incorrect'}, 'GET', '/v1/status'))
        no_certificate = ssl.create_default_context(cafile=str(root / 'ca.pem'))
        no_certificate.minimum_version = ssl.TLSVersion.TLSv1_3
        must_fail(lambda: request({**nodes[0], 'tls_context': no_certificate}, 'GET', '/v1/status'))
        untrusted = ssl.create_default_context()
        untrusted.load_cert_chain(str(root / 'client.pem'), str(root / 'client.key'))
        must_fail(lambda: request({**nodes[0], 'tls_context': untrusted}, 'GET', '/v1/status'))
        journal = Journal(root / 'journal', nodes)
        try:
            assert journal.run(nodes)['states'] == ['verified']
            assert journal.run(nodes)['states'] == ['verified']
            assert engine.generation == 2
            if live:
                assert len(engine.publication_audit()['events']) == 1
                assert 'netforge_tx_failures_total 0\n' in engine.snapshot()
            else:
                assert engine.posts == 1
        finally:
            journal.close()
        changed = [{**nodes[0], 'transport_identity': '0' * 64}]
        must_fail(lambda: Journal(root / 'journal', changed))
        manifest.append(dict(manifest[0]))
        (root / 'members.json').write_text(json.dumps(manifest))
        must_fail(lambda: load_plan(root / 'members.json'))
        (root / 'server.key').chmod(0o644)
        must_fail(lambda: SecureServer(('127.0.0.1', 0), engine, 'node-b', root / 'server.pem', root / 'server.key', root / 'ca.pem'))
    finally:
        server.shutdown(); server.server_close(); thread.join(3)
        assert not thread.is_alive()
        if live:
            engine.close()
print('PASS: real TLS1.3 mutual authentication, bearer and node binding, private-key refusal, static membership and journaled idempotent rollout')
print('PASS: missing client certificate, untrusted server, wrong node/token and changed transport plan refused')

if len(sys.argv) == 3:
    print('PASS: actual reference dataplane acknowledged TLS journal publication, idempotent reconciliation and metrics on private veth')
