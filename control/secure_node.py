#!/usr/bin/env python3
"""Explicit opt-in TLS1.3/mTLS management; no discovery or automatic exposure."""
import argparse
import http.server
import json
import re
import pathlib
import signal
import ssl
from agent import Agent, Handler
from durable_agent import private_read


def node_id(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}', value):
        raise ValueError('node ID must be 1..64 ASCII letters/digits/_.-')
    return value


class NodeView:
    def __init__(self, agent, name):
        self.agent, self.name = agent, node_id(name)

    def __getattr__(self, key):
        return getattr(self.agent, key)

    def status(self):
        return {**self.agent.status(), 'node_id': self.name}

    def configuration(self):
        return {**self.agent.configuration(), 'node_id': self.name}

    def publish(self, expected, data):
        return {**self.agent.publish(expected, data), 'node_id': self.name}


class SecureServer(http.server.HTTPServer):
    def __init__(self, address, agent, name, certificate, key, ca):
        # Reject public/group-readable keys before binding or starting children.
        private_read(key, 65536)
        for path in (certificate, ca):
            with pathlib.Path(path).open('rb') as source:
                if len(source.read(65537)) > 65536:
                    raise ValueError('certificate material exceeds64KiB')
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = ssl.TLSVersion.TLSv1_3
        self.context.verify_mode = ssl.CERT_REQUIRED
        self.context.load_verify_locations(cafile=str(ca))
        self.context.load_cert_chain(str(certificate), str(key))
        self.agent = NodeView(agent, name)
        super().__init__(address, Handler)
        self.timeout = 0.25

    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(2)
        try:
            return self.context.wrap_socket(connection, server_side=True), address
        except BaseException:
            connection.close()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('binary', 'config', 'state', 'node-id', 'certificate', 'key', 'ca'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=9443)
    args = parser.parse_args()
    # Validate TLS and bind before starting the dataplane; no plaintext fallback.
    agent = Agent(args.binary, args.config, args.state)
    server = None
    try:
        server = SecureServer((args.bind, args.port), agent, args.node_id,
                              args.certificate, args.key, args.ca)
        agent.start()
        stopped = False
        def stop(*_):
            nonlocal stopped
            stopped = True
        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        print(json.dumps({'event': 'secure_node_started', 'node_id': args.node_id,
                          'port': server.server_port}), flush=True)
        while not stopped and agent.child.poll() is None:
            server.handle_request()
    finally:
        if server:
            server.server_close()
        agent.close()


if __name__ == '__main__':
    main()
