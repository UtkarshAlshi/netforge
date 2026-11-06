#!/usr/bin/env python3
"""Bounded, loopback-only management agent for one compatible NetForge dataplane process."""
import argparse
import collections
import hmac
import hashlib
import http.server
import json
import os
import pathlib
import queue
import secrets
import signal
import subprocess
import tempfile
import threading
import time

MAX_CONFIG = 65536


def atomic(path, data):
    fd, name = tempfile.mkstemp(prefix=".pending-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(name, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(name):
            os.unlink(name)


class Agent:
    def __init__(self, binary, initial, state):
        self.binary = str(pathlib.Path(binary).resolve(strict=True))
        self.state = pathlib.Path(state)
        # Dedicated new directory: never overwrite an existing deployment.
        self.state.mkdir(mode=0o700)
        self.active = self.state / "active.conf"
        self.events = queue.Queue(maxsize=64)
        self.logs = collections.deque(maxlen=128)
        self.metrics = collections.deque(maxlen=256)
        self.audit = collections.deque(maxlen=64)
        self.audit_sequence = 0
        self.condition = threading.Condition()
        self.generation = None
        self.uncertain = False
        self.child = None
        with pathlib.Path(initial).open("rb") as source:
            data = source.read(MAX_CONFIG + 1)
        self.validate(data)
        atomic(self.active, data)
        self.token = secrets.token_urlsafe(32)
        atomic(self.state / "token", self.token.encode())

    def record_publication(self, request_id, expected, candidate_hash, http_status):
        with self.condition:
            self.audit_sequence += 1
            record = {"sequence": self.audit_sequence, "timestamp_ns": time.time_ns(),
                      "request_id": request_id,
                      "expected_generation": expected if type(expected) is int and 0 < expected < 2**64 else None,
                      "candidate_sha256": candidate_hash, "applied_generation": self.generation,
                      "http_status": http_status, "uncertain": bool(self.uncertain),
                      "outcome": "applied" if http_status == 200 else
                                 "uncertain" if self.uncertain else
                                 "rejected" if http_status in (400, 409) else "unavailable"}
            self.audit.append(record)

    def publication_audit(self):
        with self.condition:
            return {"events": [dict(event) for event in self.audit],
                    "last_sequence": self.audit_sequence,
                    "evicted": self.audit_sequence - len(self.audit), "capacity": 64,
                    "persistent": False}

    def validation_command(self, path):
        return [self.binary, "check", str(path)]

    def runtime_command(self):
        return [self.binary, "run", str(self.active)]

    def validate(self, data):
        if not data or len(data) > MAX_CONFIG:
            raise ValueError("configuration must be 1..65536 bytes")
        fd, name = tempfile.mkstemp(dir=self.state, prefix=".validate-")
        try:
            with os.fdopen(fd, "wb") as out:
                out.write(data)
            result = subprocess.run(self.validation_command(name), stdout=subprocess.PIPE,
                                    stderr=subprocess.DEVNULL, timeout=5, check=False)
            if result.returncode:
                raise ValueError("configuration validation failed")
            generation = json.loads(result.stdout)["generation"]
            if type(generation) is not int or generation < 1:
                raise ValueError("invalid generation")
            return generation
        finally:
            os.unlink(name)

    def start(self):
        self.child = subprocess.Popen(self.runtime_command(), stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, bufsize=0)
        self.reader = threading.Thread(target=self.read_logs, daemon=True)
        self.reader.start()
        event = self.await_event({"started"}, 5)
        self.generation = event["generation"]

    def read_logs(self):
        # Binary readline limit also bounds a faulty child's unterminated output.
        while True:
            raw = self.child.stdout.readline(4097)
            if not raw:
                break
            if len(raw) > 4096:
                self.uncertain = True
                break
            line = raw.decode("utf-8", errors="replace").strip()
            with self.condition:
                if line.startswith("netforge_"):
                    self.metrics.append(line)
                else:
                    self.logs.append(line)
                self.condition.notify_all()
            try:
                event = json.loads(line)
                if isinstance(event, dict) and event.get("event") in {
                    "started", "config_published", "config_rejected", "stopped"
                }:
                    self.events.put_nowait(event)
            except (ValueError, queue.Full):
                pass

    def await_event(self, names, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            try:
                event = self.events.get(timeout=min(0.1, max(0.001, end - time.monotonic())))
                if event.get("event") in names:
                    return event
            except queue.Empty:
                if self.child.poll() is not None:
                    raise RuntimeError("dataplane exited")
        raise TimeoutError("dataplane acknowledgement timed out")

    def status(self):
        return {"generation": self.generation, "running": self.child.poll() is None,
                "uncertain": self.uncertain, "pid": self.child.pid}

    def configuration(self):
        if self.uncertain or self.child.poll() is not None:
            raise RuntimeError("dataplane unavailable or publication uncertain")
        with self.active.open("rb") as source:
            data = source.read(MAX_CONFIG + 1)
        if len(data) > MAX_CONFIG:
            raise RuntimeError("active configuration exceeds bounds")
        return {"generation": self.generation, "config": data.decode("utf-8")}

    def publish(self, expected, data):
        if type(expected) is not int or expected != self.generation:
            raise ValueError("generation conflict")
        if self.uncertain or self.child.poll() is not None:
            raise RuntimeError("dataplane unavailable or publication uncertain")
        generation = self.validate(data)
        if generation <= self.generation:
            raise ValueError("generation must increase")
        previous = self.active.read_bytes()
        atomic(self.active, data)
        try:
            self.child.send_signal(signal.SIGHUP)
            event = self.await_event({"config_published", "config_rejected"}, 3)
        except (TimeoutError, RuntimeError, OSError):
            # Never report rollback: the child may have published without an ACK.
            self.uncertain = True
            raise RuntimeError("publication uncertain; inspect and restart agent before retrying")
        if event["event"] == "config_rejected":
            atomic(self.active, previous)
            raise ValueError("dataplane rejected update; previous file restored")
        if event.get("generation") != generation:
            self.uncertain = True
            raise RuntimeError("unexpected acknowledged generation")
        self.generation = generation
        return self.status()

    def snapshot(self):
        if self.child.poll() is not None:
            raise RuntimeError("dataplane unavailable")
        with self.condition:
            self.metrics.clear()
            self.child.send_signal(signal.SIGUSR1)
            end = time.monotonic() + 2
            while not any(x.startswith("netforge_rx_oversize_total ") or x == "netforge_metrics_snapshot_complete 1"
                          for x in self.metrics):
                remaining = end - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("metrics timed out")
                self.condition.wait(remaining)
            return "\n".join(self.metrics) + "\n"

    def close(self):
        if self.child is None:
            return
        if self.child.poll() is None:
            self.child.terminate()
            try:
                self.child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.child.kill()
                self.child.wait(timeout=2)
        self.reader.join(timeout=1)
        self.child.stdout.close()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def setup(self):
        super().setup()
        self.connection.settimeout(2)

    def log_message(self, *_):
        pass  # Never log credentials, candidate content or arbitrary request paths.

    def reply(self, status, body, content_type="application/json"):
        if content_type == "application/json":
            body = json.dumps(body)
        encoded = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        if getattr(self, "publication_id", None):
            self.send_header("X-Netforge-Request-ID", self.publication_id)
        self.end_headers()
        self.wfile.write(encoded)

    def authorized(self):
        if not hmac.compare_digest(self.headers.get("Authorization", ""), "Bearer " + self.server.agent.token):
            self.reply(401, {"error": "unauthorized"})
            return False
        return True

    def do_GET(self):
        if not self.authorized():
            return
        try:
            if self.path == "/v1/status":
                self.reply(200, self.server.agent.status())
            elif self.path == "/v1/config":
                self.reply(200, self.server.agent.configuration())
            elif self.path == "/v1/audit":
                self.reply(200, self.server.agent.publication_audit())
            elif self.path == "/metrics":
                self.reply(200, self.server.agent.snapshot(), "text/plain; version=0.0.4")
            else:
                self.reply(404, {"error": "unknown endpoint"})
        except (RuntimeError, TimeoutError, OSError) as error:
            self.reply(503, {"error": str(error)})

    def do_POST(self):
        if not self.authorized():
            return
        if self.path != "/v1/config":
            self.reply(404, {"error": "unknown endpoint"})
            return
        self.publication_id = secrets.token_hex(16)
        self.publication_expected = None
        self.publication_hash = None
        supplied_id = self.headers.get("X-Netforge-Request-ID")
        if supplied_id is not None:
            if not 1 <= len(supplied_id) <= 64 or not all(c.isascii() and (c.isalnum() or c in "-_.") for c in supplied_id):
                self.publication_reply(400, {"error": "invalid request correlation ID"})
                return
            self.publication_id = supplied_id
        try:
            if self.headers.get("Transfer-Encoding"):
                raise ValueError("chunked requests unsupported")
            length = int(self.headers.get("Content-Length", "0"))
            if length < 1 or length > 80000:
                raise ValueError("request size exceeds bounds")
            raw = self.rfile.read(length)
            if len(raw) != length:
                raise ValueError("incomplete request")
            request = json.loads(raw)
            if not isinstance(request, dict) or set(request) != {"expected_generation", "config"} or not isinstance(request["config"], str):
                raise ValueError("expected generation and configuration required")
            self.publication_expected = request["expected_generation"]
            data = request["config"].encode()
            if 1 <= len(data) <= MAX_CONFIG:
                self.publication_hash = hashlib.sha256(data).hexdigest()
            result = self.server.agent.publish(request["expected_generation"], data)
            status, body = 200, result
        except (ValueError, KeyError, TypeError) as error:
            status, body = 409, {"error": str(error)}
        except (RuntimeError, TimeoutError, OSError, subprocess.TimeoutExpired) as error:
            status, body = 503, {"error": str(error)}
        self.publication_reply(status, body)

    def publication_reply(self, status, body):
        # Record the engine/API outcome before attempting response I/O. A client
        # disconnect does not reverse an acknowledged publication or prove delivery.
        self.server.agent.record_publication(self.publication_id, self.publication_expected,
                                             self.publication_hash, status)
        self.reply(status, {**body, "request_id": self.publication_id})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--state", required=True, help="new, dedicated state directory")
    parser.add_argument("--port", type=int, default=9080)
    parser.add_argument("--workers", type=int, choices=(1, 2), help="opt-in bounded worker dataplane")
    args = parser.parse_args()
    if args.workers:
        from worker_agent import WorkerAgent
        agent = WorkerAgent(args.binary, args.config, args.state, args.workers)
    else:
        agent = Agent(args.binary, args.config, args.state)
    server = None
    try:
        agent.start()
        server = http.server.HTTPServer(("127.0.0.1", args.port), Handler)
        server.agent = agent
        server.timeout = 0.25
        stopped = False

        def stop(*_):
            nonlocal stopped
            stopped = True

        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        print(json.dumps({"event": "agent_started", "port": server.server_port}), flush=True)
        while not stopped and agent.child.poll() is None:
            server.handle_request()
    finally:
        if server:
            server.server_close()
        agent.close()


if __name__ == "__main__":
    main()
