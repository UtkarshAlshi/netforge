"""Actual DPDK multi-rule/multi-route capture and config/policy mismatch refusal."""
import json
from pathlib import Path
import struct
import subprocess
import sys
engine, compiler, reference, project, directory = sys.argv[1:]
root=Path(directory);root.mkdir(exist_ok=False)
base=(Path(project)/'examples/xdp-veth.conf').read_text()
old=next(line for line in base.splitlines() if line.startswith('acl '))
config=root/'multi.conf'
config.write_text(base.replace(old,'\n'.join(f'acl {action} lan udp 10.88.0.0/24 0.0.0.0/0 * {port}' for action,port in [('deny',9002),('allow',9000),('allow',9001)]))+'\nroute 10.90.0.0/24 wan 10.89.0.2\n')
policy=root/'multi.bin'
subprocess.run([compiler,str(config),'lan',str(policy),'lan=1','wan=2'],check=True,capture_output=True,timeout=5)
def checksum(data):
    if len(data)%2:data+=b'\0'
    total=sum(struct.unpack('!'+str(len(data)//2)+'H',data))
    while total>>16:total=(total&65535)+(total>>16)
    return (~total)&65535
frames=[]
for i,(port,dest) in enumerate([(9000,'10.89.0.2'),(9001,'10.90.0.9'),(9002,'10.89.0.2'),(9003,'10.90.0.9')]):
    dst=bytes(map(int,dest.split('.')))
    udp=struct.pack('!HHHH',12000+i,port,30,0)+bytes(range(22))
    header=bytearray(struct.pack('!BBHHHBBH4s4s',0x45,0,50,7,0,64,17,0,bytes([10,88,0,2]),dst))
    struct.pack_into('!H',header,10,checksum(bytes(header)))
    frames.append(bytes.fromhex('0288000000020288000000010800')+bytes(header)+udp)
header=struct.pack('<IHHIIII',0xa1b2c3d4,2,4,0,0,65535,1)
source=root/'input.pcap';source.write_bytes(header+b''.join(struct.pack('<IIII',1700000000,i,len(f),len(f))+f for i,f in enumerate(frames)))
hexfile=root/'input.hex';hexfile.write_text(''.join(f.hex()+'\n' for f in frames))
result=subprocess.run([reference,'replay',str(config),'lan',str(hexfile)],check=True,capture_output=True,text=True,timeout=5)
decisions=[json.loads(line) for line in result.stdout.splitlines()]
expected=[bytes.fromhex(d['frame']) for d in decisions if d['egress']]
assert len(expected)==2 and all(d['reason']=='acl_deny' for d in decisions[2:])
output,punts=root/'routed.pcap',root/'punted.pcap'
run=subprocess.run([engine,str(config),str(policy),'--pcap',str(source),str(output),str(punts)],check=True,capture_output=True,text=True,timeout=15)
raw=output.read_bytes();offset=24;actual=[];timestamps=[]
while offset<len(raw):
    sec,usec,n,original=struct.unpack_from('<IIII',raw,offset);offset+=16
    assert n==original and offset+n<=len(raw)
    actual.append(raw[offset:offset+n]);timestamps.append((sec,usec));offset+=n
assert actual==expected and timestamps==[(1700000000,0),(1700000000,1)]
assert punts.stat().st_size==24 and '"received":4' in run.stdout and '"dropped":2' in run.stdout
wrong=root/'wrong.bin';blob=bytearray(policy.read_bytes());blob[12]^=1;wrong.write_bytes(blob)
refused=root/'must-not-exist.pcap'
rejected=subprocess.run([engine,str(config),str(wrong),'--pcap',str(source),str(refused),str(root/'no-punt.pcap')],capture_output=True,text=True,timeout=10)
assert rejected.returncode!=0 and 'does not match configuration' in rejected.stderr and not refused.exists()
(root/'acceptance.log').write_text(run.stdout+run.stderr+rejected.stdout+rejected.stderr)
print('PASS: actual DPDK three-rule/two-route capture, independent byte/timestamp parity and pre-EAL mismatch refusal')
