#!/usr/bin/env python3
"""Render one bounded saved NetForge metrics snapshot; no server or network access."""
import argparse
import html
import math
import re
from pathlib import Path

SERIES = re.compile(r'^(netforge_[a-zA-Z0-9_]+(?:\{[^\n{}]{0,512}\})?) ([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)$')


def parse(text):
    if len(text.encode('utf-8')) > 256*1024:
        raise ValueError('snapshot exceeds 256 KiB')
    values = {}
    for line in text.splitlines():
        if not line.startswith('netforge_'):
            continue
        match = SERIES.fullmatch(line)
        if not match:
            raise ValueError('malformed metric line')
        key, raw = match.groups()
        value = float(raw)
        if not math.isfinite(value) or value < 0:
            raise ValueError('metric must be finite and nonnegative')
        if key in values:
            raise ValueError('duplicate series: supply one snapshot, not concatenated logs')
        values[key] = raw
        if len(values) > 256:
            raise ValueError('snapshot exceeds 256 series')
    if not values:
        raise ValueError('no NetForge metrics found')
    return values


def render(values):
    rows = []
    for name, value in sorted(values.items()):
        base = name.split('{', 1)[0]
        group = ('Backend traffic' if base.startswith('netforge_backend_') else
                 'Backend health' if base.startswith('netforge_health_') else
                 'Kernel decisions' if base.startswith('netforge_xdp_') else
                 'Socket I/O' if base.startswith(('netforge_port_', 'netforge_tx_', 'netforge_rx_', 'netforge_packet_socket_')) else
                 'Process' if base.startswith('netforge_process_') else
                 'Worker state' if base.startswith('netforge_worker_') else 'Core state and decisions')
        rows.append('<tr><td>'+group+'</td><td><code>'+html.escape(name)+'</code></td><td>'+html.escape(value)+'</td></tr>')
    return '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>NetForge metrics snapshot</title><style>body{font:16px system-ui;max-width:1200px;margin:30px auto;padding:16px;color:#19283d}table{border-collapse:collapse;width:100%}td,th{padding:10px;border-bottom:1px solid #ddd;text-align:left}code{overflow-wrap:anywhere}aside{padding:15px;background:#fff3cd}</style><h1>NetForge metrics snapshot</h1><aside>Saved observation, not a live monitor. Missing metrics are unavailable, not zero. Counters reset on restart. Socket sends and XDP decisions do not prove delivery or measure end-to-end loss.</aside><p>Filter by metric, label or group: <input id="filter" aria-label="Filter metrics" maxlength="100"></p><table><thead><tr><th>Group</th><th>Series</th><th>Observed value</th></tr></thead><tbody>'''+''.join(rows)+'''</tbody></table><script>document.getElementById('filter').addEventListener('input',function(){const q=this.value.toLowerCase();for(const row of document.querySelectorAll('tbody tr'))row.hidden=!row.textContent.toLowerCase().includes(q);});</script></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('output', type=Path, help='new HTML file; overwrite refused')
    args = parser.parse_args()
    with args.snapshot.open('rb') as source:
        data = source.read(256*1024+1)
    if len(data) > 256*1024:
        raise SystemExit('STOP: input exceeds 256 KiB')
    page = render(parse(data.decode('utf-8')))
    with args.output.open('x') as output:
        output.write(page)
    print('SAVED', args.output)

if __name__ == '__main__':
    main()
