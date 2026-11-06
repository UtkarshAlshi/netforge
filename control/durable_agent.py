#!/usr/bin/env python3
"""Opt-in durable single-agent recovery. Restarts lose dataplane flow state."""
import argparse
import base64
import fcntl
import hashlib
import http.server
import json
import os
import pathlib
import re
import secrets
import select
import signal
import stat
import subprocess
import threading
from agent import Agent, Handler, MAX_CONFIG, atomic


def fingerprint(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def private_read(path, limit):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, "rb") as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
            raise ValueError("persistent file must be private and owned by current UID")
        data = source.read(limit + 1)
    if len(data) > limit:
        raise ValueError("persistent file exceeds bounds")
    return data


def identity(pid):
    try:
        values = pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        if values[0] == "Z":
            return None
        return {"pid": pid, "start": int(values[19]),
                "boot": pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip()}
    except (FileNotFoundError, ProcessLookupError):
        return None
    except (OSError, ValueError, IndexError) as error:
        raise RuntimeError("cannot safely inspect persisted child identity") from error


def stop_owned(record, authorized):
    if record is None:
        return
    if not isinstance(record, dict) or set(record) != {"pid", "start", "boot"} or type(record["pid"]) is not int or record["pid"] <= 1:
        raise ValueError("invalid persisted child identity")
    if identity(record["pid"]) != record:
        return  # Exited/reused PID; never signal an unrelated process.
    if not authorized:
        raise RuntimeError("owned dataplane is still running; explicit --stop-owned-child is required")
    if not hasattr(os, "pidfd_open") or not hasattr(signal, "pidfd_send_signal"):
        raise RuntimeError("safe orphan recovery requires Linux pidfd support")
    descriptor = os.pidfd_open(record["pid"], 0)
    try:
        if identity(record["pid"]) != record:
            return
        poller = select.poll(); poller.register(descriptor, select.POLLIN)
        signal.pidfd_send_signal(descriptor, signal.SIGTERM)
        if not poller.poll(3000):
            signal.pidfd_send_signal(descriptor, signal.SIGKILL)
            if not poller.poll(2000):
                raise RuntimeError("owned dataplane did not exit; refusing second instance")
    finally:
        os.close(descriptor)


def checkpoint(data, generation, committed):
    return {"data": base64.b64encode(data).decode("ascii"), "generation": generation,
            "sha256": hashlib.sha256(data).hexdigest(), "committed": committed}


def revision(data, generation):
    lines = data.decode("utf-8").splitlines(keepends=True)
    matches = [index for index, line in enumerate(lines) if line.split("#", 1)[0].split()[:1] == ["version"]]
    if len(matches) != 1:
        raise ValueError("checkpoint requires one version directive")
    lines[matches[0]] = f"version {generation}\n"
    return "".join(lines).encode()


class DurableAgent(Agent):
    def __init__(self, binary, initial, state, resume=False, stop_owned_child=False):
        self.root = pathlib.Path(state)
        self.lock_fd = None
        self.child = None
        self.store = None
        self.recovered = resume
        try:
            if not resume:
                self.root.mkdir(mode=0o700)
            info = self.root.lstat()
            if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
                raise ValueError("state directory must be private, owned and not a symlink")
            self.lock_fd = os.open(self.root / "lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
            fcntl.flock(self.lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            digest = fingerprint(binary)
            entries = list(self.root.iterdir())
            if len(entries) > 64 or sum(path.name.startswith("instance-") for path in entries) >= 16:
                raise RuntimeError("retained instance bound reached; operator review required")
            if resume:
                envelope = json.loads(private_read(self.root / "journal.json", 2 * MAX_CONFIG + 8192))
                self.store = envelope["record"]
                if envelope.get("sha256") != hashlib.sha256(json.dumps(self.store, sort_keys=True).encode()).hexdigest():
                    raise ValueError("journal integrity failure")
                if self.store.get("schema") != 1 or self.store.get("binary_sha256") != digest:
                    raise ValueError("journal schema or binary fingerprint mismatch")
                saved = self.store["checkpoint"]
                data = base64.b64decode(saved["data"], validate=True)
                if not data or len(data) > MAX_CONFIG or saved["sha256"] != hashlib.sha256(data).hexdigest():
                    raise ValueError("checkpoint integrity failure")
                if type(saved["generation"]) is not int or not 1 <= saved["generation"] <= 0xffffffff:
                    raise ValueError("invalid checkpoint generation")
                if self.store.get("phase") not in {"prepared", "launching", "running", "publishing", "uncertain", "stopped"}:
                    raise ValueError("invalid durable phase")
                if self.store["phase"] in {"launching", "running", "publishing", "uncertain"} and self.store.get("child") is None:
                    raise RuntimeError("child identity missing in launch/active phase; operator reconciliation required")
                token = private_read(self.root / "token", 128).decode("ascii")
                if not re.fullmatch(r"[A-Za-z0-9_-]{32,128}", token):
                    raise ValueError("invalid persisted token")
            else:
                with pathlib.Path(initial).open("rb") as source:
                    data = source.read(MAX_CONFIG + 1)
                token = secrets.token_urlsafe(32)
            data.decode("utf-8")  # The HTTP/config recovery contract is UTF-8.
            atomic(self.root / "startup.conf", data)
            Agent.__init__(self, binary, self.root / "startup.conf", self.root / ("instance-" + secrets.token_hex(8)))
            generation = self.validate(data)
            if resume:
                if generation != saved["generation"]:
                    raise ValueError("checkpoint revision does not match its configuration")
                high_water = self.store.get("high_water")
                if type(high_water) is not int or not generation <= high_water < 0xffffffff:
                    raise ValueError("invalid or exhausted revision high-water mark")
                generation = high_water + 1
                data = revision(data, generation)
                self.validate(data)
                stop_owned(self.store.get("child"), stop_owned_child)
                atomic(self.active, data)
                self.store.update(phase="prepared", child=None, candidate=None, high_water=generation)
            else:
                self.store = {"schema": 1, "binary_sha256": digest, "phase": "prepared", "child": None,
                              "high_water": generation, "candidate": None, "checkpoint": checkpoint(data, generation, False)}
            self.token = token
            atomic(self.root / "token", token.encode())
            atomic(self.state / "token", token.encode())
            self.write_journal()
        except BaseException:
            self.release_lock()
            raise

    def write_journal(self):
        encoded = json.dumps(self.store, sort_keys=True).encode()
        atomic(self.root / "journal.json", json.dumps({"record": self.store, "sha256": hashlib.sha256(encoded).hexdigest()}, sort_keys=True).encode())

    def status(self):
        result = super().status()
        result.update(durable_high_water=self.store["high_water"], recovered=self.recovered)
        return result

    def release_lock(self):
        if self.lock_fd is not None:
            os.close(self.lock_fd); self.lock_fd = None

    def start(self):
        expected = self.validate(self.active.read_bytes())
        self.store.update(phase="launching", child=None)
        self.write_journal()  # Unknown-PID crash window is explicit and fail-closed.
        try:
            self.child = subprocess.Popen([self.binary, "run", str(self.active)], stdout=subprocess.PIPE,
                                          stderr=subprocess.STDOUT, bufsize=0)
            self.reader = threading.Thread(target=self.read_logs, daemon=True); self.reader.start()
            self.store["child"] = identity(self.child.pid)
            self.write_journal()
            event = self.await_event({"started"}, 5)
            if event.get("generation") != expected:
                raise RuntimeError("unexpected startup acknowledgement")
            self.store.update(phase="running", candidate=None, checkpoint=checkpoint(self.active.read_bytes(), expected, True))
            self.write_journal()
            self.generation = expected
        except BaseException:
            self.uncertain = True
            raise

    def publish(self, expected, data):
        if type(expected) is not int or expected != self.generation:
            raise ValueError("generation conflict")
        if self.uncertain or self.child.poll() is not None:
            raise RuntimeError("dataplane unavailable or publication uncertain")
        generation = self.validate(data)
        if generation <= self.store["high_water"]:
            raise ValueError("generation must exceed durable high-water mark")
        self.store.update(phase="publishing", high_water=generation,
                          candidate={"generation": generation, "sha256": hashlib.sha256(data).hexdigest()})
        try:
            self.write_journal()  # Persist intent before file replacement/signal.
            result = super().publish(expected, data)
        except ValueError:
            self.store.update(phase="running", candidate=None)
            try:
                self.write_journal()
            except OSError:
                self.uncertain = True
                raise
            raise
        except BaseException:
            self.uncertain = True
            raise
        self.store.update(phase="running", candidate=None, checkpoint=checkpoint(data, generation, True))
        try:
            self.write_journal()  # No successful HTTP response before this fsync.
        except OSError:
            self.uncertain = True
            raise
        return result

    def close(self):
        try:
            if self.child is not None:
                super().close()
            if self.lock_fd is not None and self.store is not None:
                self.store.update(phase="stopped", child=None)
                self.write_journal()
        finally:
            self.release_lock()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--config", help="initial config; checkpoint is authoritative on resume")
    parser.add_argument("--state", required=True)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--stop-owned-child", action="store_true", help="allow pidfd termination of exactly identified prior child")
    parser.add_argument("--port", type=int, default=9080)
    args = parser.parse_args()
    if not args.resume and not args.config:
        parser.error("--config is required for a fresh deployment")
    agent = DurableAgent(args.binary, args.config, args.state, args.resume, args.stop_owned_child)
    server = None
    try:
        agent.start()
        server = http.server.HTTPServer(("127.0.0.1", args.port), Handler)
        server.agent = agent; server.timeout = 0.25
        stopped = False
        def stop(*_):
            nonlocal stopped
            stopped = True
        signal.signal(signal.SIGTERM, stop); signal.signal(signal.SIGINT, stop)
        print(json.dumps({"event": "durable_agent_started", "port": server.server_port, "generation": agent.generation}), flush=True)
        while not stopped and agent.child.poll() is None:
            server.handle_request()
    finally:
        if server: server.server_close()
        agent.close()


if __name__ == "__main__":
    main()
