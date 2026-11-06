"""Actual portable replay → browser walkthrough and renderer trust boundaries."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
binary, project = sys.argv[1:]
project = Path(project); sys.path.insert(0, str(project / 'tools'))
from render_demo import render
from summarize_network import summarize
with tempfile.TemporaryDirectory() as temp:
    output = Path(temp) / 'demo'
    subprocess.run([sys.executable, str(project / 'tools/demo.py'), '--binary', binary, '--output', str(output)], check=True, timeout=30)
    packets = output / 'packets'; report = json.loads((packets / 'report.json').read_text())
    assert set(report['cases']) == {'route', 'nat', 'service', 'ttl', 'malformed'}
    page = (packets / 'index.html').read_text()
    assert all(value in page for value in ('TTL 63', '10.77.0.254:40000', 'tracked_udp', 'ttl_exceeded', 'ethernet_truncated'))
    assert '<script' not in page and 'http://' not in page and 'https://' not in page
    decisions = packets / 'route.jsonl'; value = json.loads(decisions.read_text())
    value['reason'] = '</code><script>alert(1)</script>'; decisions.write_text(json.dumps(value) + '\n')
    rendered = render(packets); assert '<script>' not in rendered and '&lt;script&gt;' in rendered
    report['cases']['route']['verified'] = False; (packets / 'report.json').write_text(json.dumps(report))
    try: render(packets)
    except ValueError: pass
    else: raise AssertionError('unverified result rendered as success')
    rerun = subprocess.run([sys.executable, str(project / 'tools/demo.py'), '--binary', binary, '--output', str(output)], capture_output=True, timeout=5)
    assert rerun.returncode != 0, 'existing output overwritten'
raw = (project / 'results/portable-0.1.0/network-trials.json').read_bytes()
summary = summarize(raw); assert summary['verified_packets'] == 3072 and len(summary['conditions']) == 24
altered = json.loads(raw); altered['trials'][0]['packets_received'] = 63
try: summarize(json.dumps(altered).encode())
except ValueError: pass
else: raise AssertionError('missing packet summarized as verified')
print('PASS: real replay HTML, exclusive launcher output, escaped/unverified rendering and retained-result validation')
