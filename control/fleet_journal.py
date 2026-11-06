#!/usr/bin/env python3
"""Locked rollout journal; interrupted publications are reconciled, never retried blindly."""
import argparse
import fcntl
import hashlib
import json
import os
import pathlib
import stat

from agent import atomic
from durable_agent import private_read
from fleet import load_plan, request


def digest(data):
    return hashlib.sha256(data).hexdigest()


def intent(nodes):
    if not 1 <= len(nodes) <= 16:
        raise ValueError('fleet must contain 1..16 nodes')
    result = []
    for node in nodes:
        versions = [line.split()[1:] for line in node['body'].splitlines()
                    if line.split() and line.split()[0] == 'version']
        if len(versions) != 1 or len(versions[0]) != 1:
            raise ValueError('exactly one version directive required')
        generation = int(versions[0][0])
        if not node['expected_generation'] < generation <= 0xffffffff:
            raise ValueError('candidate must advance expected generation')
        result.append({'name': node['name'], 'url': node['url'],
                       'expected': node['expected_generation'], 'generation': generation,
                       'sha256': digest(node['body'].encode())})
    return result


class Journal:
    def __init__(self, root, nodes):
        self.root = pathlib.Path(root)
        self.lock = None
        self.root.mkdir(mode=0o700, exist_ok=True)
        info = self.root.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise RuntimeError('journal requires a private owned directory')
        fd = os.open(self.root / 'lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        self.lock = os.fdopen(fd, 'r+')
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
                raise RuntimeError('unsafe lock file')
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.plan = intent(nodes)
            self.path = self.root / 'rollout.json'
            if self.path.exists() or self.path.is_symlink():
                self.record = json.loads(private_read(self.path, 32768))
                if (set(self.record) != {'schema', 'plan', 'states'} or self.record['schema'] != 1
                        or self.record['plan'] != self.plan or len(self.record['states']) != len(nodes)
                        or any(s not in ('pending', 'intended', 'verified') for s in self.record['states'])):
                    raise RuntimeError('journal does not match exact rollout plan')
            else:
                self.record = {'schema': 1, 'plan': self.plan, 'states': ['pending'] * len(nodes)}
                self.save()
        except BaseException:
            self.close()
            raise

    def save(self):
        atomic(self.path, json.dumps(self.record, sort_keys=True).encode())

    def close(self):
        if self.lock:
            self.lock.close()
            self.lock = None

    def run(self, nodes):
        if intent(nodes) != self.plan:
            raise RuntimeError('run does not match journal plan')
        # Complete read-only fleet preflight before the first publication. No global transaction.
        for i, (node, target) in enumerate(zip(nodes, self.plan)):
            status = request(node, 'GET', '/v1/status')
            current = request(node, 'GET', '/v1/config')
            if (not status.get('running') or status.get('uncertain')
                    or current.get('generation') != status.get('generation')):
                raise RuntimeError('unavailable/inconsistent agent: ' + node['name'])
            matches = (status['generation'] == target['generation']
                       and digest(current['config'].encode()) == target['sha256'])
            if matches:
                self.record['states'][i] = 'verified'
                self.save()
            elif self.record['states'][i] != 'pending':
                raise RuntimeError('interrupted or drifted node requires a new operator-reviewed plan: ' + node['name'])
            elif (status['generation'] != target['expected']
                  or status.get('durable_high_water', status['generation']) >= target['generation']):
                raise RuntimeError('generation/high-water conflict: ' + node['name'])
        for i, (node, target) in enumerate(zip(nodes, self.plan)):
            if self.record['states'][i] == 'verified':
                continue
            self.record['states'][i] = 'intended'
            self.save()  # Durable intent precedes HTTP; any failure requires reconciliation.
            body = json.dumps({'expected_generation': target['expected'], 'config': node['body']}).encode()
            if len(body) > 80000:
                raise ValueError('encoded request exceeds bound')
            status = request(node, 'POST', '/v1/config', body)
            current = request(node, 'GET', '/v1/config')
            if (not status.get('running') or status.get('uncertain')
                    or status.get('generation') != target['generation']
                    or current.get('generation') != target['generation']
                    or digest(current['config'].encode()) != target['sha256']):
                raise RuntimeError('publication acknowledgement/configuration mismatch')
            self.record['states'][i] = 'verified'
            self.save()
        return self.record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest')
    parser.add_argument('state', help='dedicated private rollout directory; retain for reconciliation')
    args = parser.parse_args()
    nodes = load_plan(args.manifest)
    journal = Journal(args.state, nodes)
    try:
        print(json.dumps(journal.run(nodes), indent=2))
    finally:
        journal.close()


if __name__ == '__main__':
    main()
