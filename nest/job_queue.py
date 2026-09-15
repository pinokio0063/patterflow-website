"""Shared nest job line: up to MAX_RUN jobs in parallel; extras wait for a slot."""
import threading
import uuid


MAX_RUN = 4


class JobQueue(object):
    def __init__(self, max_run=MAX_RUN):
        self.max_run = int(max_run)
        self._slots = threading.Semaphore(self.max_run)
        self._st = threading.Lock()
        self.waiting = 0
        self.running = 0
        self._jobs = {}

    def snapshot(self, ticket=None):
        ticket = str(ticket or "").strip() or None
        with self._st:
            wait = self.waiting
            run = self.running
            mine = self._jobs.get(ticket) if ticket else None
            ahead = 0
            if mine == "waiting":
                for t, st in self._jobs.items():
                    if t == ticket:
                        break
                    if st == "waiting":
                        ahead += 1
        if mine == "waiting":
            msg = "Server: waiting for a free slot · %s ahead · %s/%s running" % (
                ahead, run, self.max_run
            )
        elif mine == "running":
            msg = "Server: your job is running · %s/%s slots" % (run, self.max_run)
        elif run and wait:
            msg = "Server: %s running, %s waiting (max %s together)" % (
                run, wait, self.max_run
            )
        elif run:
            msg = "Server: %s/%s running" % (run, self.max_run)
        elif wait:
            msg = "Server: %s waiting" % wait
        else:
            msg = "Server: ready."
        return {
            "ok": True,
            "running": run,
            "waiting": wait,
            "position": run + wait,
            "max": self.max_run,
            "status": mine or "",
            "ahead": ahead if mine == "waiting" else 0,
            "message": msg,
        }

    def acquire(self, ticket=None):
        ticket = str(ticket or "").strip() or str(uuid.uuid4())
        with self._st:
            self._jobs[ticket] = "waiting"
            self.waiting += 1
        self._slots.acquire()
        with self._st:
            self.waiting = max(0, self.waiting - 1)
            self.running += 1
            self._jobs[ticket] = "running"
        return ticket

    def release(self, ticket=None):
        ticket = str(ticket or "").strip() or None
        with self._st:
            if ticket:
                self._jobs.pop(ticket, None)
            self.running = max(0, self.running - 1)
        self._slots.release()
