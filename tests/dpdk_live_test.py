"""Real DPDK AF_PACKET PMD packets on private hosted veths; no physical NIC."""
import json
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
binary,compiler,reference,project,report = sys.argv[1:]
project=Path(project);root=Path(tempfile.mkdtemp(prefix='netforge-dpdk-live-private-'))
config=project/'examples/xdp-veth.conf';policy=root/'policy.bin'
subprocess.run([compiler,str(config),'lan',str(policy),'lan=1','wan=2'],check=True,capture_output=True,timeout=5)

def checksum(data):
    if len(data)%2:data+=b'\0'
    total=sum(struct.unpack('!'+str(len(data)//2)+'H',data))
    while total>>16:total=(total&65535)+(total>>16)
    return (~total)&65535

def frame(size,port=9000,ttl=64):
    udp=struct.pack('!HHHH',12000,port,size-34,0)+bytes((i%251 for i in range(size-42)))
    ip=bytearray(struct.pack('!BBHHHBBH4s4s',0x45,0,size-14,7,0,ttl,17,0,bytes([10,88,0,2]),bytes([10,89,0,2])))
    struct.pack_into('!H',ip,10,checksum(bytes(ip)))
    return bytes.fromhex('0288000000020288000000010800')+bytes(ip)+udp

source=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(0x0800));source.bind(('nf-source',0))
sink=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(0x0800));sink.bind(('nf-sink',0));sink.settimeout(3)
process=subprocess.Popen([binary,str(config),str(policy),'nf-in','nf-out','20'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
    assert select.select([process.stdout],[],[],15)[0],'DPDK service startup timeout'
    line=process.stdout.readline()
    if not line:
        raise AssertionError('DPDK startup failed: '+process.stderr.read(8192))
    started=json.loads(line);assert started['event']=='dpdk_live_started',started
    positives=[]
    for size in (64,128,512,1514):
        packet=frame(size);hexfile=root/'oracle.hex';hexfile.write_text(packet.hex()+'\n')
        oracle=subprocess.run([reference,'replay',str(config),'lan',str(hexfile)],check=True,capture_output=True,text=True,timeout=5)
        decision=json.loads(oracle.stdout.splitlines()[0]);assert decision['egress']=='wan'
        expected=bytes.fromhex(decision['frame'])
        source.send(packet);actual=sink.recv(2048);assert actual==expected,(size,len(actual))
        positives.append(size)
        if size==64:
            for denied in (frame(64,9001),frame(64,ttl=1)):
                source.send(denied);assert not select.select([sink],[],[],.2)[0],'deny/exception leaked to egress'
            bad=bytearray(frame(64));bad[24]^=1;source.send(bad)
            assert not select.select([sink],[],[],.2)[0],'invalid checksum leaked to egress'
    process.send_signal(signal.SIGTERM)
    stdout,stderr=process.communicate(timeout=8)
    assert process.returncode==0,(stdout,stderr)
    metrics=next(json.loads(line) for line in stdout.splitlines() if line.startswith('{"mode"'))
    assert metrics['routed']==4 and metrics['sent']==4 and metrics['unsent']==0 and metrics['dropped']>=2
    assert metrics['exceptions_discarded']>=1 and metrics['pool_reclaimed']==255
    assert metrics['received']==metrics['routed']+metrics['dropped']+metrics['exceptions_discarded']
    Path(report).write_text(json.dumps({'mode':'real AF_PACKET PMD/private veth','positive_sizes':positives,'metrics':metrics,'physical_nic':False},indent=2)+'\n')
    print('PASS: real continuous DPDK AF_PACKET PMD RX/TX at64/128/512/1514 bytes matches independent reference bytes')
    print('PASS: ACL/checksum deny and TTL exception absent at egress; SIGTERM, all255 mbufs and EAL cleanup verified')
finally:
    if process.poll() is None:
        process.terminate()
        try:process.wait(timeout=3)
        except subprocess.TimeoutExpired:process.kill();process.wait(timeout=3)
    source.close();sink.close()
