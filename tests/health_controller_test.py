"""Real checker plus deterministic agent/probe doubles for health reconciliation."""
import pathlib
import sys
import tempfile
sys.dont_write_bytecode = True
binary = pathlib.Path(sys.argv[1]).resolve()
project = pathlib.Path(sys.argv[2]).resolve()
sys.path.insert(0, str(project / "control"))
from health_controller import run_round


class Agent:
    def __init__(self, config):
        self.config, self.generation = config, 1
        self.posts = 0
        self.reject, self.uncertain = False, False
    def request(self, method, path, value=None):
        assert path == "/v1/config"
        if method == "GET":
            return {"config": self.config, "generation": self.generation}
        assert value["expected_generation"] == self.generation
        self.posts += 1
        if self.reject:
            raise RuntimeError("generation conflict or rejection")
        self.config = value["config"]
        self.generation += 1
        return {"generation": self.generation, "running": True, "uncertain": self.uncertain}


base = (project / "examples" / "tcp-appliance.conf").read_text()
with tempfile.TemporaryDirectory(prefix="health-controller-", dir=binary.parent) as name:
    root = pathlib.Path(name)
    api = Agent(base)
    def down(targets, timeout):
        assert 0.05 <= timeout <= 0.5
        return {target: not target.startswith("10.77.0.130:") for target in targets}
    def up(targets, timeout):
        return {target: True for target in targets}
    def run(output, previous=None, observer=down):
        return run_round(api, binary, ["10.77.0.128/25"], root / output,
                         root / previous if previous else None, fall=2, rise=2, observer=observer)
    result = run("one")
    assert not result["published"] and api.posts == 0 and api.generation == 1
    first_metrics = (root / "one" / "metrics.prom").read_text()
    assert 'netforge_health_config_generation 1\n' in first_metrics
    assert 'netforge_health_probe_up{endpoint="10.77.0.130:9000"} 0\n' in first_metrics
    assert 'netforge_health_failure_streak{endpoint="10.77.0.130:9000"} 1\n' in first_metrics
    result = run("two", "one")
    assert result["published"] and api.generation == 2 and "10.77.0.130:9000/drain" in api.config
    assert 'netforge_health_round_published 1\n' in (root / "two" / "metrics.prom").read_text()
    result = run("three", "two", up)
    assert not result["published"] and api.generation == 2
    result = run("four", "three", up)
    assert result["published"] and api.generation == 3 and "10.77.0.130:9000/drain" not in api.config
    recovery_metrics = (root / "four" / "metrics.prom").read_text()
    assert 'netforge_health_config_generation 3\n' in recovery_metrics
    assert 'netforge_health_success_streak{endpoint="10.77.0.130:9000"} 2\n' in recovery_metrics
    api.config += "\n# manual edit\n"
    try:
        run("mismatch", "four")
        raise AssertionError("manual config drift was not rejected")
    except RuntimeError:
        pass
    assert not (root / "mismatch").exists()
    api = Agent(base)
    run("reject-one")
    api.reject = True
    try:
        run("reject-two", "reject-one")
        raise AssertionError("rejected publication counted as accepted")
    except RuntimeError:
        pass
    assert not (root / "reject-two" / "accepted.json").exists()
    assert not (root / "reject-two" / "metrics.prom").exists()
    assert not (root / "reject-two" / "state.json").exists()
    assert api.generation == 1
    api = Agent(base)
    run("uncertain-one")
    api.uncertain = True
    try:
        run("uncertain-two", "uncertain-one")
        raise AssertionError("uncertain publication counted as accepted")
    except RuntimeError:
        pass
    assert api.generation == 2  # It may have applied: never pretend a rollback.
    assert not (root / "uncertain-two" / "accepted.json").exists()
    assert not (root / "uncertain-two" / "metrics.prom").exists()
    try:
        run("seed-uncertain", "uncertain-two")
        raise AssertionError("unacknowledged round reused")
    except FileNotFoundError:
        pass
    api = Agent(base)
    called = []
    def no_probe(*_):
        called.append(True)
        raise AssertionError("out-of-scope probe executed")
    try:
        run_round(api, binary, ["10.78.0.0/24"], root / "scope", observer=no_probe)
        raise AssertionError("invalid probe scope accepted")
    except ValueError:
        pass
    assert not called and not (root / "scope").exists()
print("PASS: health-to-API publication, hysteresis, drift detection, rejection and uncertain-state isolation")
