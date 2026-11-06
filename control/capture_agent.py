#!/usr/bin/env python3
"""Loopback control of bounded finite DPDK capture jobs, not a live NIC daemon."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import secrets
import re
import selectors
import signal
import stat
import subprocess
import threading
import time
from agent import Handler, atomic


def bounded_file(path, limit):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            raise ValueError('regular input file required')
        data = source.read(limit + 1)
    if not data or len(data) > limit:
        raise ValueError('capture/config input exceeds service bound')
    return data


class CaptureAgent:
    def __init__(self, binary, compiler, config, capture, state):
        if not hasattr(os, 'pidfd_open') or not hasattr(signal, 'pidfd_send_signal'):
            raise RuntimeError('Linux pidfd support required for capture child ownership')
        self.binary = str(Path(binary).resolve())
        self.state = Path(state)
        self.state.mkdir(mode=0o700, exist_ok=False)
        self.token = secrets.token_urlsafe(32)
        atomic(self.state/'token', self.token.encode())
        self.config = bounded_file(config, 65536)
        self.input = bounded_file(capture, 1024*1024)
        atomic(self.state/'config.conf', self.config)
        atomic(self.state/'input.pcap', self.input)
        run = subprocess.run([str(compiler), str(self.state/'config.conf'), 'lan', str(self.state/'policy.bin'), 'lan=1', 'wan=2'], capture_output=True, timeout=5, check=True)
        if len(run.stdout)+len(run.stderr) > 65536:
            raise RuntimeError('compiler output exceeded bound')
        checked = subprocess.run([self.binary, str(self.state/'config.conf'), str(self.state/'policy.bin'), '--check'], capture_output=True, timeout=5, check=True)
        self.generation = json.loads(checked.stdout)['generation']
        self.lock = threading.Lock()
        self.jobs = []
        self.child = None
        self.pidfd = None
        self.thread = None
        self.closed = False

    def status(self):
        with self.lock:
            return {'kind':'finite-dpdk-capture', 'generation':self.generation,
                    'config_sha256':hashlib.sha256(self.config).hexdigest(),
                    'input_sha256':hashlib.sha256(self.input).hexdigest(),
                    'jobs':[dict(job) for job in self.jobs], 'capacity':4,
                    'persistent_recovery':False}

    def start(self, expected, request_id):
        with self.lock:
            if self.closed or type(expected) is not int or expected != self.generation:
                raise RuntimeError('closed agent or stale configuration')
            if not isinstance(request_id,str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,64}',request_id):
                raise RuntimeError('bounded request_id required')
            for prior in self.jobs:
                if prior['request_id']==request_id:
                    return {'id':prior['id'],'state':prior['state']}
            if self.child is not None and self.child.poll() is None:
                raise RuntimeError('one capture job at a time')
            if self.thread is not None and self.thread.is_alive():
                raise RuntimeError('previous job is finishing')
            if len(self.jobs) == 4:
                raise RuntimeError('four-job session budget exhausted')
            job_id = len(self.jobs)+1
            directory = self.state/('job-'+str(job_id)); directory.mkdir(mode=0o700)
            child = subprocess.Popen([self.binary, str(self.state/'config.conf'), str(self.state/'policy.bin'), '--pcap', str(self.state/'input.pcap'), str(directory/'routed.pcap'), str(directory/'punted.pcap')], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try: descriptor=os.pidfd_open(child.pid)
            except OSError:
                child.kill();child.wait(timeout=2);child.stdout.close();raise
            job = {'id':job_id, 'request_id':request_id, 'state':'running', 'exit_code':None, 'stats':None}
            self.child = child; self.pidfd=descriptor; self.jobs.append(job)
            self.thread = threading.Thread(target=self._watch, args=(child, descriptor, job, directory), daemon=True)
            try:self.thread.start()
            except RuntimeError:
                self._signal(descriptor,signal.SIGKILL);child.wait(timeout=2)
                child.stdout.close();os.close(descriptor);self.pidfd=None;self.thread=None
                job.update(state='failed',exit_code=child.returncode)
                raise
            return {'id':job_id, 'state':'running'}

    @staticmethod
    def _signal(descriptor, value):
        try: signal.pidfd_send_signal(descriptor,value)
        except ProcessLookupError: pass

    def _watch(self, child, descriptor, job, directory):
        data = bytearray(); failure = None
        deadline = time.monotonic()+30
        selector = selectors.DefaultSelector()
        selector.register(child.stdout, selectors.EVENT_READ)
        try:
            while True:
                if time.monotonic() >= deadline:
                    failure = 'deadline'; self._signal(descriptor,signal.SIGKILL); break
                if not selector.select(.1):
                    continue
                chunk = os.read(child.stdout.fileno(),4096)
                if not chunk: break
                if len(data)+len(chunk)>65536:
                    failure='log_limit';self._signal(descriptor,signal.SIGKILL);break
                data.extend(chunk)
            code = child.wait(timeout=2)
            atomic(directory/'runtime.log', bytes(data))
            stats = None
            for line in data.decode(errors='replace').splitlines():
                if line.startswith('{"mode"'):
                    value = json.loads(line)
                    keys = ['received','routed','dropped','punted','sent','unsent']
                    if value.get('mode')=='pcap' and type(value.get('complete')) is bool and all(type(value.get(k)) is int and 0<=value[k]<=65536 for k in keys):
                        stats = {k:value[k] for k in ['complete',*keys]}
            with self.lock:
                cancelled = job['state']=='stopping'
                job.update(state='completed' if code==0 and stats and stats['complete'] and not failure else 'cancelled' if cancelled else 'failed', exit_code=code, stats=stats)
                if failure: job['failure']=failure
                atomic(directory/'result.json',json.dumps(job).encode())
        except Exception:
            if child.poll() is None: self._signal(descriptor,signal.SIGKILL)
            child.wait(timeout=2)
            with self.lock: job.update(state='failed', exit_code=child.returncode)
        finally:
            selector.close();child.stdout.close()
            with self.lock:
                os.close(descriptor)
                if self.child is child:self.pidfd=None

    def stop(self, job_id):
        with self.lock:
            if not self.jobs or type(job_id) is not int or self.jobs[-1]['id'] != job_id:
                raise RuntimeError('only the current exact job can be stopped')
            if self.child.poll() is None:
                self.jobs[-1]['state']='stopping'; self._signal(self.pidfd,signal.SIGTERM)
            return {'id':job_id,'state':self.jobs[-1]['state']}

    def close(self):
        with self.lock:
            self.closed=True
            if self.child is not None and self.child.poll() is None:
                self.jobs[-1]['state']='stopping';self._signal(self.pidfd,signal.SIGTERM)
            child, worker = self.child, self.thread
        if child is not None:
            try: child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                with self.lock:
                    if self.pidfd is not None:self._signal(self.pidfd,signal.SIGKILL)
                child.wait(timeout=2)
        if worker is not None: worker.join(timeout=3)


class CaptureHandler(Handler):
    def do_GET(self):
        if not self.authorized():return
        if self.path=='/v1/status':self.reply(200,self.server.agent.status())
        else:self.reply(404,{'error':'unknown endpoint'})

    def do_POST(self):
        if not self.authorized():return
        try:
            length = int(self.headers.get('Content-Length','0'))
            if not 1<=length<=128 or self.headers.get('Transfer-Encoding'):
                raise ValueError('bounded request body required')
            body=json.loads(self.rfile.read(length))
            if self.path=='/v1/jobs' and isinstance(body,dict) and set(body)=={'expected_generation','request_id'}:
                self.reply(202,self.server.agent.start(body['expected_generation'],body['request_id']))
            elif self.path=='/v1/stop' and isinstance(body,dict) and set(body)=={'id'}:
                self.reply(200,self.server.agent.stop(body['id']))
            else:self.reply(400,{'error':'invalid request'})
        except (ValueError,RuntimeError,TypeError,OSError) as error:
            self.reply(409,{'error':str(error)})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for option in ('binary','compiler','config','input','state'):parser.add_argument('--'+option,required=True)
    parser.add_argument('--port',type=int,default=9081)
    args=parser.parse_args()
    agent=CaptureAgent(args.binary,args.compiler,args.config,args.input,args.state)
    server=http.server.HTTPServer(('127.0.0.1',args.port),CaptureHandler)
    server.agent=agent
    def interrupted(*_):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
    print(json.dumps({"event":"capture_agent_started","port":server.server_port}),flush=True)
    try:server.serve_forever(poll_interval=.1)
    except KeyboardInterrupt:pass
    finally:server.server_close();agent.close()

if __name__=='__main__':main()
