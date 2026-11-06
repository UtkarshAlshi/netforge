#!/usr/bin/env python3
"""Static, explicit mTLS membership and bounded observations; no consensus."""
import argparse
import concurrent.futures
import hashlib
import http.client
import json
import pathlib
import ssl
import time
import urllib.parse
from durable_agent import private_read
from secure_node import node_id
from fleet import request


def load_plan(path):
    path = pathlib.Path(path).resolve()
    with path.open('rb') as source:
        raw = source.read(65537)
    if len(raw) > 65536:
        raise ValueError('membership exceeds64KiB')
    plan = json.loads(raw)
    if not isinstance(plan, list) or not 1 <= len(plan) <= 16:
        raise ValueError('membership requires1..16 nodes')
    nodes, ids, endpoints = [], set(), set()
    fields = {'name', 'url', 'node_id', 'ca', 'certificate', 'key', 'token_file', 'config', 'expected_generation'}
    for item in plan:
        if not isinstance(item, dict) or set(item) != fields:
            raise ValueError('invalid secure membership fields')
        name, identity = node_id(item['name']), node_id(item['node_id'])
        url = urllib.parse.urlsplit(item['url'])
        if (url.scheme != 'https' or not url.hostname or not url.port or url.username is not None
                or url.password is not None or url.path not in ('', '/') or url.query or url.fragment):
            raise ValueError('explicit HTTPS host and port required')
        endpoint = (url.hostname, url.port)
        if identity in ids or endpoint in endpoints or any(n['name'] == name for n in nodes):
            raise ValueError('duplicate member identity/name/endpoint')
        if type(item['expected_generation']) is not int or not 1 <= item['expected_generation'] <= 0xffffffff:
            raise ValueError('invalid expected generation')
        token = private_read(path.parent / item['token_file'], 130).decode().strip()
        if not 16 <= len(token) <= 128 or not all(c.isascii() and (c.isalnum() or c in '_-') for c in token):
            raise ValueError('invalid private token')
        private_read(path.parent / item['key'], 65536)
        context = ssl.create_default_context(cafile=str(path.parent / item['ca']))
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        context.load_cert_chain(str(path.parent / item['certificate']), str(path.parent / item['key']))
        with (path.parent / item['config']).open('rb') as source:
            config = source.read(65537)
        if not 1 <= len(config) <= 65536:
            raise ValueError('configuration exceeds bounds')
        transport = hashlib.sha256()
        for key in ('ca', 'certificate'):
            with (path.parent / item[key]).open('rb') as source:
                content = source.read(65537)
            if len(content) > 65536:
                raise ValueError('certificate exceeds bounds')
            transport.update(len(content).to_bytes(4, 'big')); transport.update(content)
        transport.update(identity.encode())
        nodes.append({**item, 'host': url.hostname, 'port': url.port, 'tls_context': context,
                      'token': token, 'body': config.decode(), 'sha256': hashlib.sha256(config).hexdigest(),
                      'transport_identity': transport.hexdigest()})
        ids.add(identity); endpoints.add(endpoint)
    return nodes


def observe(nodes):
    def check(node):
        started = time.monotonic()
        result = {'name': node['name'], 'node_id': node['node_id'], 'reachable': False}
        try:
            status = request(node, 'GET', '/v1/status')
            result.update(reachable=True, running=bool(status.get('running')),
                          uncertain=bool(status.get('uncertain')), generation=status.get('generation'))
        except (OSError, ValueError, RuntimeError, http.client.HTTPException) as error:
            result['error'] = type(error).__name__
        result['observation_seconds'] = round(time.monotonic() - started, 6)
        return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        return list(executor.map(check, nodes))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('observe', 'rollout'))
    parser.add_argument('manifest')
    parser.add_argument('--journal')
    args = parser.parse_args()
    nodes = load_plan(args.manifest)
    if args.action == 'observe':
        print(json.dumps(observe(nodes), indent=2))
    else:
        if not args.journal:
            parser.error('rollout requires an explicit private --journal directory')
        from fleet_journal import Journal
        journal = Journal(args.journal, nodes)
        try:
            print(json.dumps(journal.run(nodes), indent=2))
        finally:
            journal.close()


if __name__ == '__main__':
    main()
