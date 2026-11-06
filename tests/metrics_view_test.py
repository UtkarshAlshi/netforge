"""Offline metric safety and interpretation checks; no sockets needed."""
import importlib.util
from pathlib import Path

path = Path(__file__).resolve().parents[1]/'tools/metrics_view.py'
spec = importlib.util.spec_from_file_location('metrics_view', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
values = module.parse('noise\nnetforge_tx_failures_total 0\nnetforge_process_resident_bytes 1880064\n')
assert len(values) == 2 and 'netforge_udp_mappings' not in values
page = module.render(module.parse('netforge_decisions_total{reason="<script>"} 1\n'))
assert '&lt;script&gt;' in page and 'reason="<script>"' not in page
for text in ['netforge_x NaN', 'netforge_x -1', 'netforge_x 1e999', 'netforge_x 1\nnetforge_x 2',
             'netforge_x 1\n'+'x'*262144, '\n'.join(f'netforge_x_{i} 1' for i in range(257)), 'only noise']:
    try:
        module.parse(text)
    except ValueError:
        continue
    raise AssertionError('unsafe or ambiguous snapshot accepted')
print('PASS: bounded metrics, duplicate/reset ambiguity refusal, missing-value semantics and HTML escaping')
