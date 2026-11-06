"""Real config checker, deterministic runtime double, durable restart/crash gates."""
import hashlib
import json
import pathlib
import signal
import subprocess
import sys
import tempfile
import time

binary, project = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
sys.dont_write_bytecode = True
sys.path.insert(0, str(project / "control"))
from durable_agent import DurableAgent, identity, stop_owned


def rejected(function):
    try:
        function()
    except (ValueError, RuntimeError, OSError, KeyError):
        return
    raise AssertionError("unsafe operation accepted")


def rewrite(path, record):
    data = json.dumps(record, sort_keys=True).encode()
    path.write_text(json.dumps({"record": record, "sha256": hashlib.sha256(data).hexdigest()}))


with tempfile.TemporaryDirectory(prefix="netforge-durable-", dir=binary.parent) as directory:
    root = pathlib.Path(directory)
    base = (project / "examples/xdp-veth.conf").read_text()
    initial = root / "initial.conf"; initial.write_text(base)
    fake = root / "fake-engine"
    fake.write_text(f'''#!{sys.executable}
import json, os, pathlib, signal, sys, time
if sys.argv[1] == "check": os.execv({str(binary)!r}, [{str(binary)!r}] + sys.argv[1:])
path = pathlib.Path(sys.argv[2]); generation = int(path.read_text().split("version ")[1].split()[0]); stop = False
def emit(value): print(json.dumps(value), flush=True)
def reload(*_):
 global generation
 data = path.read_text()
 if "reject-runtime" in data: emit({{"event":"config_rejected"}}); return
 generation = int(data.split("version ")[1].split()[0])
 if "omit-ack" not in data: emit({{"event":"config_published", "generation":generation}})
def metrics(*_):
 print("netforge_config_generation " + str(generation), flush=True)
 print("netforge_rx_oversize_total 0", flush=True)
def end(*_):
 global stop
 stop = True
signal.signal(signal.SIGHUP, reload); signal.signal(signal.SIGTERM, end); signal.signal(signal.SIGUSR1, metrics)
emit({{"event":"started", "generation":generation}})
while not stop: time.sleep(.01)
try: emit({{"event":"stopped"}})
except BrokenPipeError: pass
''')
    fake.chmod(0o700)
    state = root / "state"
    agent = DurableAgent(fake, initial, state)
    token = agent.token
    try:
        agent.start()
        rejected(lambda: DurableAgent(fake, None, state, resume=True))
        candidate = base.replace("version 1", "version 2") + "\n# committed checkpoint\n"
        assert agent.publish(1, candidate.encode())["generation"] == 2
        assert (state / "journal.json").stat().st_mode & 0o777 == 0o600
        rejected(lambda: agent.publish(2, (base.replace("version 1", "version 3") + "\n# reject-runtime\n").encode()))
        assert agent.generation == 2 and not agent.uncertain
        assert agent.status()["durable_high_water"] == 3
        rejected(lambda: agent.publish(2, base.replace("version 1", "version 3").encode()))
    finally:
        agent.close()
    resumed = DurableAgent(fake, None, state, resume=True)
    try:
        resumed.start()
        assert resumed.token == token and resumed.generation == 4
        assert "committed checkpoint" in resumed.configuration()["config"]
        assert "reject-runtime" not in resumed.configuration()["config"]
        assert resumed.status()["recovered"]
    finally:
        resumed.close()
    journal = state / "journal.json"
    intact = journal.read_bytes()
    corrupted = json.loads(intact); corrupted["record"]["high_water"] += 1
    journal.write_text(json.dumps(corrupted))
    rejected(lambda: DurableAgent(fake, None, state, resume=True))
    journal.write_bytes(intact)
    record = json.loads(intact)["record"]; record.update(phase="launching", child=None)
    rewrite(journal, record)
    rejected(lambda: DurableAgent(fake, None, state, resume=True))
    journal.write_bytes(intact)
    print("PASS: durable ACK checkpoint, private files, writer lock, rejection, token retention, monotonic restart and corrupt/unknown-launch refusal")

    # Simulate fsync failure before a publication can receive success.
    failed = DurableAgent(fake, initial, root / "io-failure")
    try:
        failed.start(); write = failed.write_journal
        def fail(): raise OSError("injected journal fsync failure")
        failed.write_journal = fail
        rejected(lambda: failed.publish(1, base.replace("version 1", "version 2").encode()))
        assert failed.uncertain
        rejected(lambda: failed.publish(1, base.replace("version 1", "version 3").encode()))
        failed.write_journal = write
    finally:
        failed.close()
    print("PASS: durable write failure blocks success and subsequent publications")

    if sys.platform == "linux" and hasattr(__import__("os"), "pidfd_open") and hasattr(signal, "pidfd_send_signal"):
        crash_state = root / "crash-state"
        guardian = root / "guardian.py"
        guardian.write_text(f'''import sys, time, json
sys.dont_write_bytecode = True
sys.path.insert(0, {str(project / "control")!r})
from durable_agent import DurableAgent
agent = DurableAgent({str(fake)!r}, {str(initial)!r}, {str(crash_state)!r})
agent.start()
base = {base!r}
agent.publish(1, (base.replace("version 1", "version 2") + "\\n# committed checkpoint\\n").encode())
try: agent.publish(2, (base.replace("version 1", "version 3") + "\\n# omit-ack candidate-must-not-survive\\n").encode())
except RuntimeError: pass
print(json.dumps(agent.status()), flush=True)
while True: time.sleep(1)
''')
        owner = subprocess.Popen([sys.executable, str(guardian)], stdout=subprocess.PIPE, text=True)
        recovered = None; prior = None
        try:
            import select
            ready, _, _ = select.select([owner.stdout], [], [], 8)
            assert ready, "crash fixture startup deadline"
            status = json.loads(owner.stdout.readline()); assert status["uncertain"]
            prior = identity(status["pid"]); assert prior is not None
            owner.kill(); owner.wait(timeout=3)
            rejected(lambda: DurableAgent(fake, None, crash_state, resume=True))
            recovered = DurableAgent(fake, None, crash_state, resume=True, stop_owned_child=True)
            recovered.start()
            assert identity(status["pid"]) != prior
            assert recovered.generation == 4
            assert "committed checkpoint" in recovered.configuration()["config"]
            assert "candidate-must-not-survive" not in recovered.configuration()["config"]
        finally:
            if owner.poll() is None: owner.kill(); owner.wait(timeout=3)
            owner.stdout.close()
            if recovered: recovered.close()
            if prior: stop_owned(prior, True)
        print("PASS: killed supervisor/lost ACK, explicit exact-child pidfd stop and higher-revision acknowledged-checkpoint recovery")
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(20)"])
        another = None
        try:
            record = json.loads(journal.read_text())["record"]
            old_identity = identity(unrelated.pid); assert old_identity is not None
            old_identity["start"] += 1  # A reused PID does not match the saved start time.
            record.update(phase="running", child=old_identity)
            rewrite(journal, record)
            another = DurableAgent(fake, None, state, resume=True, stop_owned_child=True)
            another.start()
            assert unrelated.poll() is None
        finally:
            if another: another.close()
            unrelated.terminate(); unrelated.wait(timeout=3)
        print("PASS: mismatched/reused PID identity never signals unrelated process")
    else:
        print("SKIP: pidfd orphan recovery requires Linux; clean restart tests passed")
