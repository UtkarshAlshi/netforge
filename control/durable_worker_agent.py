#!/usr/bin/env python3
"""Durable worker configuration recovery; flow state is deliberately not persisted."""
import argparse
import http.server
import json
import signal
from agent import Handler
from durable_agent import DurableAgent


class DurableWorkerAgent(DurableAgent):
    def __init__(self,binary,initial,state,workers,resume=False,stop_owned_child=False):
        if type(workers) is not int or workers not in (1,2):
            raise ValueError('workers must be integer1 or2')
        self.workers=workers
        super().__init__(binary,initial,state,resume,stop_owned_child)

    def runtime_identity(self):
        return {'mode':'workers','workers':self.workers}

    def validation_command(self,path):
        return [self.binary,'check-workers',str(path),str(self.workers)]

    def runtime_command(self):
        return [self.binary,'run-workers',str(self.active),str(self.workers)]

    def status(self):
        return {**super().status(),'workers':self.workers,'flow_state_persisted':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',required=True)
    parser.add_argument('--config')
    parser.add_argument('--state',required=True)
    parser.add_argument('--workers',type=int,choices=(1,2),required=True)
    parser.add_argument('--resume',action='store_true')
    parser.add_argument('--stop-owned-child',action='store_true')
    parser.add_argument('--port',type=int,default=9080)
    args=parser.parse_args()
    if not args.resume and not args.config:parser.error('--config required for new deployment')
    agent=DurableWorkerAgent(args.binary,args.config,args.state,args.workers,args.resume,args.stop_owned_child)
    server=None
    try:
        agent.start()
        server=http.server.HTTPServer(('127.0.0.1',args.port),Handler);server.agent=agent;server.timeout=.25
        stopped=False
        def stop(*_):
            nonlocal stopped
            stopped=True
        signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
        print(json.dumps({'event':'durable_worker_started','port':server.server_port,'generation':agent.generation}),flush=True)
        while not stopped and agent.child.poll() is None:server.handle_request()
    finally:
        if server:server.server_close()
        agent.close()

if __name__=='__main__':main()
