#!/usr/bin/env python3
"""Build a bounded offline evidence index; never runs Docker or changes the lab."""
import argparse
import hashlib
import html
import json
from pathlib import Path

CASES = [
    ('Integrated Linux regression', 'integration-closure-20261106-112426-19612/tests.log', '100% tests passed, 0 tests failed out of 17'),
    ('XDP authenticated lifecycle', 'xdp-api-20261106-111609-18440/live.log', 'PASS: owned engine shutdown conditionally detached XDP'),
    ('Persistent TCP', 'tcp-stream-20261106-112110-19055/service.log', 'PASS persistent TCP bytes=1048576'),
    ('Worker API and audit', 'audit-20261106-104415-15619/live.log', 'PASS: authenticated audit'),
]

def inspect(root, label, relative, marker):
    path = root / relative
    result = dict(label=label, artifact=relative, status='missing')
    if path.is_file():
        if path.stat().st_size > 2 * 1024 * 1024:
            result['status'] = 'oversize'
        else:
            data = path.read_bytes()
            result.update(status='marker-confirmed' if marker in data.decode('utf-8', errors='replace') else 'marker-missing',
                          sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('output', type=Path, help='new directory; existing output refused')
    args = parser.parse_args()
    rows = [inspect(args.evidence, *case) for case in CASES]
    summary_path = args.evidence / 'index-comparison-20261020-164120-51094/summary.json'
    performance = None
    if summary_path.is_file() and summary_path.stat().st_size <= 2*1024*1024:
        performance = json.loads(summary_path.read_text())
    report = dict(kind='offline-evidence-index', automated_validation='bounded log markers only; not a rerun',
                  complete_original_roadmap=False, evidence=rows, historical_offline_performance=performance,
                  limitations=['Offline core rates are not network throughput.',
                               'XDP and DPDK implement restricted stateless policy subsets.',
                               'Local logical agents do not demonstrate separate-machine failover.',
                               'Hosted CI execution, fresh setup and remaining roadmap gates are not certified.'])
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    body = ''.join('<tr><td>'+html.escape(row['label'])+'</td><td>'+html.escape(row['status'])+'</td><td><code>'+html.escape(row['artifact'])+'</code></td></tr>' for row in rows)
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>NetForge evidence</title><style>body{font:16px system-ui;max-width:1100px;margin:40px auto;padding:20px;color:#172033}table{border-collapse:collapse;width:100%}td,th{text-align:left;border-bottom:1px solid #ccc;padding:12px}code{overflow-wrap:anywhere}aside{background:#fff3cd;padding:16px}</style><h1>NetForge verification evidence</h1><aside>Engineering snapshot. The full original roadmap remains unfinished. This page indexes retained logs; it does not rerun acceptance tests.</aside><table><thead><tr><th>Gate</th><th>Log evidence</th><th>Artifact</th></tr></thead><tbody>'''+body+'</tbody></table><h2>Claim boundaries</h2><ul>'+''.join('<li>'+html.escape(s)+'</li>' for s in report['limitations'])+'</ul><p>Machine-readable evidence and historical offline measurements: report.json.</p></html>'
    (args.output/'index.html').write_text(page)
    print(json.dumps({'saved':str(args.output), 'evidence':rows}, indent=2))

if __name__ == '__main__':
    main()
