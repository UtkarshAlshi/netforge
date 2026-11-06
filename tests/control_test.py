"""Supervisor lifecycle, real parser validation, ACK/rejection and partial rollout."""
import importlib.util
import io
import json
import pathlib
import sys
import tempfile

sys.dont_write_bytecode = True
binary = pathlib.Path(sys.argv[1]).resolve()
project = pathlib.Path(sys.argv[2]).resolve()


def module(name):
    spec = importlib.util.spec_from_file_location(name, project / "control" / (name + ".py"))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


agent = module("agent")
fleet = module("fleet")
with tempfile.TemporaryDirectory(prefix="control-", dir=binary.parent) as root:
    root = pathlib.Path(root)
    fake = root / "fake-engine"
    # Actual C++ checker validates all candidate files; fake runtime deterministically
    # simulates rejected reload and lost acknowledgements without packet privileges.
    fake.write_text(f'''#!{sys.executable}
import json, os, pathlib, signal, sys, time
if sys.argv[1] == "check": os.execv({str(binary)!r}, [{str(binary)!r}] + sys.argv[1:])
p = pathlib.Path(sys.argv[2])
generation = int(p.read_text().split("version ")[1].split()[0])
stop = False
def emit(x): print(json.dumps(x), flush=True)
def reload(*_):
 global generation
 data = p.read_text()
 if "reject-runtime" in data: emit({{"event":"config_rejected"}}); return
 if "omit-ack" in data: return
 generation = int(data.split("version ")[1].split()[0])
 emit({{"event":"config_published", "generation":generation}})
def metrics(*_):
 print("netforge_config_generation " + str(generation), flush=True)
 print("netforge_rx_oversize_total 0", flush=True)
def end(*_):
 global stop
 stop = True
signal.signal(signal.SIGHUP,reload)
signal.signal(signal.SIGUSR1,metrics)
signal.signal(signal.SIGTERM,end)
emit({{"event":"started", "generation":generation}})
while not stop: time.sleep(0.01)
emit({{"event":"stopped"}})
''')
    fake.chmod(0o700)
    initial = project / "examples" / "appliance.conf"
    base = initial.read_text()
    instance = agent.Agent(fake, initial, root / "state")
    assert (root / "state" / "token").stat().st_mode & 0o777 == 0o600
    assert (root / "state").stat().st_mode & 0o777 == 0o700
    try:
        instance.start()
        assert instance.status()["generation"] == 1
        assert "netforge_config_generation 1" in instance.snapshot()
        for expected, text in [(0, base), (1, base + "\ninvalid x"), (True, base), (1, base)]:
            try:
                instance.publish(expected, text.encode())
                raise AssertionError("invalid publication accepted")
            except ValueError:
                pass
        candidate = base.replace("version 1", "version 2")
        assert instance.publish(1, candidate.encode())["generation"] == 2
        previous = instance.active.read_bytes()
        try:
            instance.publish(2, (base.replace("version 1", "version 3") + "\n# reject-runtime\n").encode())
            raise AssertionError("runtime rejection accepted")
        except ValueError:
            pass
        assert instance.active.read_bytes() == previous and instance.status()["generation"] == 2
        # Exercise HTTP request handlers without requiring local socket permissions.
        class Request(agent.Handler):
            def __init__(self, method, path, token, body=b""):
                self.server = type("Server", (), {"agent": instance})()
                self.path = path
                self.headers = {"Authorization": token, "Content-Length": str(len(body))}
                self.rfile, self.wfile = io.BytesIO(body), io.BytesIO()
                self.code = None
            def send_response(self, code): self.code = code
            def send_header(self, *_): pass
            def end_headers(self): pass
        req = Request("GET", "/v1/status", "wrong"); req.do_GET(); assert req.code == 401
        req = Request("GET", "/v1/status", "Bearer " + instance.token); req.do_GET()
        assert req.code == 200 and json.loads(req.wfile.getvalue())["generation"] == 2
        req = Request("GET", "/v1/config", "Bearer " + instance.token); req.do_GET()
        assert req.code == 200 and json.loads(req.wfile.getvalue()) == {"generation": 2, "config": candidate}
        req = Request("POST", "/v1/config", "Bearer " + instance.token, b"[]"); req.do_POST(); assert req.code == 409
        try:
            instance.publish(2, (base.replace("version 1", "version 3") + "\n# omit-ack\n").encode())
            raise AssertionError("missing ACK accepted")
        except RuntimeError:
            pass
        assert instance.status()["uncertain"]
        try:
            instance.publish(2, base.replace("version 1", "version 4").encode())
            raise AssertionError("uncertain state allowed another publication")
        except RuntimeError:
            pass
    finally:
        instance.close()
    assert not instance.status()["running"]
    manifest = root / "fleet.json"
    candidate_path = root / "candidate.conf"; candidate_path.write_text(candidate)
    token_path = root / "fleet-token"; token_path.write_text("x" * 32)
    record = {"name": "node-a", "url": "http://127.0.0.1:9080", "token_file": "fleet-token",
              "config": "candidate.conf", "expected_generation": 1}
    manifest.write_text(json.dumps([record]))
    loaded = fleet.load_plan(manifest)
    assert loaded[0]["body"] == candidate and len(loaded[0]["sha256"]) == 64
    for records in [[record, record], [{**record, "url": "http://example.com:9080"}], [{**record, "expected_generation": True}]]:
        manifest.write_text(json.dumps(records))
        try:
            fleet.load_plan(manifest)
            raise AssertionError("unsafe fleet plan accepted")
        except ValueError:
            pass
    # Fleet result stops on failure and reports already-applied nodes honestly.
    nodes = [{"name": str(i), "expected_generation": 1, "body": candidate, "sha256": "digest"} for i in range(3)]
    calls = []
    def request(node, method, path, body=None):
        calls.append((node["name"], method))
        if method == "POST" and node["name"] == "1": raise RuntimeError("node failed")
        return {"generation": 1 if method == "GET" else 2, "running": True, "uncertain": False}
    fleet.request = request
    results = fleet.rollout(nodes)
    assert len(results) == 2 and results[0]["published"] and not results[1]["published"]
    assert ("2", "POST") not in calls
print("PASS: control-plane validation, atomic publication, ACK, rejection, uncertainty, authentication and partial rollout")
