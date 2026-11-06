#!/usr/bin/env python3
"""Explicitly named bounded Docker lab. No deletion, host mounts or privileged mode."""
import argparse
import ipaddress
import json
import re
import subprocess
from pathlib import Path


def docker(*args):
    result = subprocess.run(['docker', *args], check=True, capture_output=True, text=True, timeout=30)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['plan', 'up', 'status', 'stop'])
    parser.add_argument('--name', required=True, help='unique netforge- prefixed lab name')
    parser.add_argument('--image', default='netforge-lab:m0', help='must already exist locally')
    parser.add_argument('--subnet', default='10.79.0.0/24')
    parser.add_argument('--output', type=Path, help='new output directory required by up')
    args = parser.parse_args()
    if not re.fullmatch(r'netforge-[a-z0-9][a-z0-9-]{0,30}', args.name):
        raise SystemExit('invalid lab name')
    subnet = ipaddress.ip_network(args.subnet)
    if subnet.version != 4 or subnet.prefixlen != 24 or not subnet.is_private:
        raise SystemExit('require a canonical private IPv4 /24')
    network_name = args.name+'-net'
    names = [args.name+'-'+suffix for suffix in ('a','b','app')]
    owner = 'io.netforge.lab='+args.name
    if args.action in ('status','stop'):
        for name in names:
            info = json.loads(docker('inspect', name))[0]
            if info['Config'].get('Labels',{}).get('io.netforge.lab') != args.name:
                raise SystemExit('STOP: ownership mismatch '+name)
        if args.action=='stop':
            print(docker('stop','--time','5',*names))
        else:
            print(docker('ps','-a','--filter','label='+owner,'--format','table {{.Names}}\t{{.Status}}'))
        return
    # Finish all read-only checks before mutation. Existing resources are never adopted.
    info = json.loads(docker('info','--format','{{json .}}'))
    if info['OSType']!='linux' or info['NCPU']>2 or info['MemTotal']>2300*1024**2:
        raise SystemExit('STOP: expected constrained Linux VM (at most2 CPUs/~2 GiB)')
    image = json.loads(docker('image','inspect',args.image))[0]
    if image.get('Os')!='linux': raise SystemExit('require local Linux lab image')
    existing = set(docker('ps','-a','--format','{{.Names}}').splitlines())
    if existing.intersection(names): raise SystemExit('STOP: container name collision')
    ids = docker('network','ls','-q').splitlines()
    if ids:
        for network in json.loads(docker('network','inspect',*ids)):
            if network['Name']==network_name: raise SystemExit('STOP: network name collision')
            for entry in network.get('IPAM',{}).get('Config',[]) or []:
                value = entry.get('Subnet')
                if value:
                    other = ipaddress.ip_network(value)
                    if other.version==4 and subnet.overlaps(other): raise SystemExit('STOP: subnet overlap '+network['Name'])
    plan = dict(name=args.name, image_id=image['Id'], network=network_name, subnet=str(subnet),
                containers=names, each=dict(memory_MiB=128,swap_MiB=0,cpus=.5,pids=64),
                boundary='Docker internal bridge; no published ports, host mounts or deletion')
    print(json.dumps(plan,indent=2),flush=True)
    if args.action=='plan': return
    if args.output is None: raise SystemExit('up requires a new --output directory')
    import os
    disk=os.statvfs(args.output.parent)
    if disk.f_bavail*disk.f_frsize<10*1024**3: raise SystemExit('STOP: below10 GiB disk headroom')
    args.output.mkdir(exist_ok=False)
    docker('network','create','--driver','bridge','--internal','--subnet',str(subnet),'--label',owner,network_name)
    def ip(host): return str(subnet.network_address+host)
    for name, host, capability in zip(names,(2,130,124),('NET_ADMIN','NET_ADMIN',None)):
        command=['run','-d','--pull','never','--name',name,'--label',owner,'--network',network_name,'--ip',ip(host),
                 '--memory','128m','--memory-swap','128m','--cpus','0.5','--pids-limit','64',
                 '--cap-drop','ALL','--cap-add','NET_RAW','--security-opt','no-new-privileges','--sysctl','net.ipv4.ip_forward=0']
        if capability: command+=['--cap-add',capability]
        # Immutable local image ID avoids mutable tag races after preflight.
        docker(*command,image['Id'],'sleep','infinity')
    for name,host,gateway in ((names[0],2,120),(names[1],130,120)):
        docker('exec',name,'ip','addr','del',ip(host)+'/24','dev','eth0')
        docker('exec',name,'ip','addr','add',ip(host)+'/25','dev','eth0')
        if host==130: docker('exec',name,'ip','route','add',ip(120)+'/32','dev','eth0')
        peer=130 if host==2 else 2
        docker('exec',name,'ip','route','add',ip(peer)+'/32','via',ip(gateway),'dev','eth0')
        docker('exec',name,'ethtool','-K','eth0','tx','off')
    observed=json.loads(docker('inspect',*names))
    macs=[entry['NetworkSettings']['Networks'][network_name]['MacAddress'] for entry in observed]
    config=f'''version 1
default deny
limits 128 30000
port lab eth0 {ip(120)}/24 {macs[2]}
route {ip(2)}/32 lab direct
route {ip(130)}/32 lab direct
neighbor lab {ip(2)} {macs[0]}
neighbor lab {ip(130)} {macs[1]}
acl allow lab udp {ip(2)}/32 {ip(130)}/32 * 9000
acl allow lab udp {ip(2)}/32 {ip(100)}/32 * 9000
snat lab {ip(120)} 40000 40031
service lab {ip(100)} 9000 {ip(130)}:9000 {ip(130)}:9001
'''
    (args.output/'appliance.conf').write_text(config)
    (args.output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    print('CREATED owned lab and config; dataplane not started. On failure retain partial resources for inspection; never auto-delete.')

if __name__=='__main__': main()
