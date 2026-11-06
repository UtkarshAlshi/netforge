"""Deterministic health hysteresis and scope; no network probes in this suite."""
import importlib.util
import pathlib
import sys
sys.dont_write_bytecode = True
project = pathlib.Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location("health", project / "control" / "health.py")
health = importlib.util.module_from_spec(spec)
spec.loader.exec_module(health)
text = """version 1
# an operator drain must survive successful health probes
tcp-service lan 10.77.0.100 9000 10.77.0.130:9000 10.77.0.131:9000/drain
service lan 10.77.0.100 9000 10.77.0.130:9000
"""
observations = {"10.77.0.130:9000": False, "10.77.0.131:9000": True}
state = {}
for generation in range(2, 5):
    text, state, changes = health.observe(text, state, observations, generation)
    assert bool(changes) == (generation == 4)
assert "10.77.0.130:9000/drain" in text and len(state["managed"]) == 1
# UDP service is not modified by TCP reachability.
assert "service lan 10.77.0.100 9000 10.77.0.130:9000\n" in text
observations["10.77.0.130:9000"] = True
text, state, changes = health.observe(text, state, observations, 5)
assert not changes and state["managed"]
text, state, changes = health.observe(text, state, observations, 6)
assert changes[0]["action"] == "recover" and not state["managed"]
assert "10.77.0.131:9000/drain" in text
# A manual drain in another service sharing the endpoint stays manual.
shared = "version 1\ntcp-service lan 10.77.0.100 9000 10.77.0.130:9000/drain\ntcp-service lan 10.77.0.101 9000 10.77.0.130:9000\n"
shared, state, _ = health.observe(shared, {}, {"10.77.0.130:9000": False}, 2, fall=1)
shared, state, _ = health.observe(shared, state, {"10.77.0.130:9000": True}, 3, rise=1)
assert "10.77.0.100 9000 10.77.0.130:9000/drain" in shared
assert "10.77.0.101 9000 10.77.0.130:9000\n" in shared
for bad_state, bad_observations, generation in [({}, {}, 7), ({"version": 2}, observations, 7), ({}, observations, 6)]:
    try:
        health.observe(text, bad_state, bad_observations, generation)
        raise AssertionError("invalid health transition accepted")
    except ValueError:
        pass
# Flapping resets consecutive failure counters.
text, state, _ = health.observe(text, {}, observations, 7)
observations["10.77.0.130:9000"] = False
text, state, changes = health.observe(text, state, observations, 8)
assert not changes
observations["10.77.0.130:9000"] = True
text, state, _ = health.observe(text, state, observations, 9)
assert state["endpoints"]["10.77.0.130:9000"]["failures"] == 0

weighted = "version 1\ntcp-service lan 10.77.0.100 9000 policy=weighted-round-robin 10.77.0.130:9000@3\n"
weighted, state, _ = health.observe(weighted, {}, {"10.77.0.130:9000": False}, 2, fall=1)
assert "policy=weighted-round-robin 10.77.0.130:9000@3/drain" in weighted
weighted, _, _ = health.observe(weighted, state, {"10.77.0.130:9000": True}, 3, rise=1)
assert "10.77.0.130:9000@3\n" in weighted

print("PASS: TCP health hysteresis, manual drain ownership, protocol isolation and bounded state")
