"""Opt-in worker adapter; uses original bounded HTTP/auth/publication contract."""
from agent import Agent


class WorkerAgent(Agent):
    def __init__(self, binary, initial, state, workers):
        if type(workers) is not int or workers not in (1, 2):
            raise ValueError('workers must be integer 1 or 2')
        self.workers = workers
        super().__init__(binary, initial, state)

    def validation_command(self, path):
        return [self.binary, 'check-workers', str(path), str(self.workers)]

    def runtime_command(self):
        return [self.binary, 'run-workers', str(self.active), str(self.workers)]

    def status(self):
        return {**super().status(), 'workers': self.workers}
