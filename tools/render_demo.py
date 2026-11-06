"""Render a bounded verified replay directory as a standalone HTML walkthrough."""
import html
import ipaddress
import json
from pathlib import Path
import struct

TITLES = {'route': '1. IPv4 routing', 'nat': '2. Source NAT', 'service': '3. Load balancing and flow affinity',
          'ttl': '4. TTL expiry', 'malformed': '5. Malformed input'}
NOTES = {'route': 'The next hop changes the Ethernet addresses; IPv4 TTL decreases from64 to63.',
         'nat': 'The private client tuple becomes10.77.0.254:40000 on egress.',
         'service': 'New flows select backend A then B. Reusing the first tuple returns to A.',
         'ttl': 'A transit packet with TTL1 produces an ICMP Time Exceeded response.',
         'malformed': 'A one-byte Ethernet input is rejected without emitting a frame.'}


def bounded(path, limit=131072):
    with Path(path).open('rb') as source:
        data = source.read(limit + 1)
    if len(data) > limit:
        raise ValueError('demo artifact exceeds rendering bound')
    return data.decode('utf-8')


def describe(value):
    data = bytes.fromhex(value)
    if len(data) < 34:
        return 'Truncated Ethernet frame (' + str(len(data)) + ' byte)'
    source, destination = str(ipaddress.IPv4Address(data[26:30])), str(ipaddress.IPv4Address(data[30:34]))
    if data[23] in (6, 17) and len(data) >= 38:
        sport, dport = struct.unpack_from('!HH', data, 34)
        source += ':' + str(sport); destination += ':' + str(dport)
    protocol = {1: 'ICMP', 6: 'TCP', 17: 'UDP'}.get(data[23], str(data[23]))
    return f'{protocol} {source} → {destination} · TTL {data[22]}'


def render(directory):
    directory = Path(directory)
    report = json.loads(bounded(directory / 'report.json', 65536))
    if set(report['cases']) != set(TITLES) or not all(c['verified'] for c in report['cases'].values()):
        raise ValueError('expected five verified demo cases')
    escape = html.escape
    cards = []
    for name, title in TITLES.items():
        inputs = bounded(directory / (name + '.hex')).splitlines()
        outputs = [json.loads(line) for line in bounded(directory / (name + '.jsonl')).splitlines()]
        if not 1 <= len(inputs) <= 3 or len(inputs) != len(outputs):
            raise ValueError('invalid demo case size')
        rows = []
        for index, (packet, decision) in enumerate(zip(inputs, outputs), 1):
            before = describe(packet)
            after = describe(decision['frame']) if decision['egress'] else 'No emitted frame'
            rows.append('<tr><td>' + str(index) + '</td><td>' + escape(before) + '</td><td>' + escape(after) +
                        '</td><td><code>' + escape(decision['reason']) + '</code></td></tr>')
        cards.append('<section><h2>' + title + ' <span>PASS</span></h2><p>' + NOTES[name] +
                     '</p><div class="scroll"><table><thead><tr><th>Packet</th><th>Input</th><th>Output</th><th>Decision</th></tr></thead><tbody>' +
                     ''.join(rows) + '</tbody></table></div><details><summary>Inspect input and decision JSON</summary><pre>' +
                     escape(json.dumps({'inputs': inputs, 'decisions': outputs}, indent=2)) + '</pre></details></section>')
    return '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NetForge · packet walkthrough</title><style>
*{box-sizing:border-box}body{margin:0;background:#0c1220;color:#e2e9f6;font:16px/1.6 system-ui,sans-serif}
main{max-width:1240px;margin:auto;padding:36px 24px}header{border-bottom:1px solid #34445c;padding-bottom:24px}
h1{font-size:42px;margin:0}h2{font-size:22px}h1 small{font-size:15px;color:#8eafca}p{max-width:90ch;color:#b9c9df}
section{background:#141f32;border:1px solid #30435e;border-radius:12px;margin:22px 0;padding:22px}
span{color:#63ddb3;font-size:13px;margin-left:10px}aside{padding:16px;border-left:4px solid #ecbe64;background:#242332}
.flow{font:17px ui-monospace,monospace;padding:18px;background:#142c39;border-radius:10px;margin:22px 0}
table{border-collapse:collapse;width:100%;min-width:800px}td,th{padding:12px;text-align:left;border-bottom:1px solid #30435e}th{color:#7cd4df}
code,pre{font-family:ui-monospace,monospace}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}
.scroll{overflow-x:auto}summary{cursor:pointer;color:#8bc5ff;margin-top:18px}footer{font-size:12px;overflow-wrap:anywhere;color:#9baec7}
</style><main><header><h1>NetForge <small>verified packet walkthrough</small></h1>
<p>A C++20 IPv4 dataplane: inspect routing, NAT, service selection and explicit rejection decisions.</p></header>
<div class="flow">Client10.77.0.2 → NetForge VIP10.77.0.100 → Backend A .130 / Backend B .131</div>
<aside>Offline synthetic replay. Five cases passed independent checksum and semantic checks.
This page demonstrates packet transformations; it does not measure live delivery or network performance.</aside>''' + ''.join(cards) + '<footer>Configuration SHA256: ' + escape(report['config_sha256']) + '<br>Executable SHA256: ' + escape(report['binary_sha256']) + '</footer></main></html>'
