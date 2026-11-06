#!/usr/bin/env python3
"""One bounded offline operator demo, with independent packet checks and retained output."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from generate_demo import frame, checksum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('output', type=Path, help='new directory; never overwrite')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary = args.binary.resolve()
    config = root/'examples/appliance.conf'
    args.output.mkdir(parents=True, exist_ok=False)
    def run(*command):
        result = subprocess.run([str(binary), *map(str, command)], check=True,
                                capture_output=True, text=True, timeout=10)
        if len(result.stdout)+len(result.stderr)>128*1024:
            raise RuntimeError('demo output exceeded bound')
        return result
    assert json.loads(run('check', config).stdout)['valid'] is True
    cases = {'route':[frame('10.77.0.130',protocol=1)],
             'nat':[frame('10.77.0.130')],
             'service':[frame('10.77.0.100',sport=p) for p in (10001,10002,10001)],
             'ttl':[frame('10.77.0.130',ttl=1,protocol=1)], 'malformed':['00']}
    report = {'kind':'offline-operator-demo', 'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
              'config_sha256':hashlib.sha256(config.read_bytes()).hexdigest(), 'cases':{},
              'boundary':'Synthetic replay only; no sockets, live packet delivery or performance claim.'}
    for name, packets in cases.items():
        source = args.output/(name+'.hex'); source.write_text('\n'.join(packets)+'\n')
        result = run('replay', config, 'lan', source)
        decisions = [json.loads(line) for line in result.stdout.splitlines()]
        assert len(decisions)==len(packets)
        for decision in decisions:
            if decision['egress']:
                emitted = bytes.fromhex(decision['frame'])
                assert checksum(emitted[14:34])==0
                if emitted[23]==17:
                    length = struct.unpack_from('!H',emitted,16)[0]-20
                    assert checksum(emitted[26:34]+b'\x00\x11'+struct.pack('!H',length)+emitted[34:34+length])==0
                elif emitted[23]==1:
                    assert checksum(emitted[34:])==0
        if name=='route': assert decisions[0]['reason']=='forward' and bytes.fromhex(decisions[0]['frame'])[22]==63
        if name=='nat': assert decisions[0]['reason']=='new_udp_mapping' and bytes.fromhex(decisions[0]['frame'])[26:30]==bytes([10,77,0,254])
        if name=='service':
            assert [bytes.fromhex(d['frame'])[33] for d in decisions]==[130,131,130]
            assert [d['reason'] for d in decisions]==['new_udp_mapping','new_udp_mapping','tracked_udp']
        if name=='ttl': assert decisions[0]['reason']=='ttl_exceeded' and bytes.fromhex(decisions[0]['frame'])[34]==11
        if name=='malformed': assert decisions[0]['reason']=='ethernet_truncated' and not decisions[0]['egress']
        (args.output/(name+'.jsonl')).write_text(result.stdout)
        (args.output/(name+'.metrics')).write_text(result.stderr)
        report['cases'][name]={'verified':True,'packets':len(packets),'reasons':[d['reason'] for d in decisions]}
    (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: five offline demo cases, independent checksums, NAT tuple and service affinity; report='+str(args.output))

if __name__=='__main__':
    main()
