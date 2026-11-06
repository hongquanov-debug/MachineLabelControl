"""
Modbus TCP link to the controller, in its own QThread.

* polls `read_status()` every settings.plc.poll_ms and emits `status`
* runs every other request (commands, recipes, jog...) from a queue so the GUI
  never blocks; results come back through `job_done(job_id, result, error)`

Usage from the GUI thread:
    job = worker.submit("save_recipe", recipe, True)      # returns job id
    worker.job_done.connect(on_done)                      # on_done(job, result, error_text)
"""
from __future__ import annotations

import itertools
import queue
import time
from typing import Any

from PyQt6.QtCore import QObject, QThread, pyqtSignal

from pmems_config.registers import MachineStatus


class PlcWorker(QThread):
    status = pyqtSignal(object)             # MachineStatus
    connection = pyqtSignal(bool, str)      # connected?, message
    job_done = pyqtSignal(int, object, str) # job id, result, error ("" = ok)

    def __init__(self, client: Any, poll_ms: int = 250, parent: QObject | None = None):
        super().__init__(parent)
        self.client = client                # PmemsClient or SimMcu (same API)
        self.poll_s = poll_ms / 1000.0
        self._jobs: "queue.Queue[tuple[int, str, tuple, dict]]" = queue.Queue()
        self._ids = itertools.count(1)
        self._stop = False
        self._connected = False

    def submit(self, method: str, *args: Any, **kwargs: Any) -> int:
        job = next(self._ids)
        self._jobs.put((job, method, args, kwargs))
        return job

    def stop(self) -> None:
        self._stop = True
        self.wait(2000)

    def _set_connected(self, ok: bool, msg: str) -> None:
        if ok != self._connected:
            self._connected = ok
            self.connection.emit(ok, msg)

    def run(self) -> None:
        next_poll = 0.0
        while not self._stop:
            if not self._connected:
                try:
                    self.client.connect()
                    self._set_connected(True, "Đã kết nối")
                except Exception as e:  # noqa: BLE001 - report any connection error
                    self.connection.emit(False, str(e))
                    time.sleep(2.0)
                    continue
            # jobs first: user actions must feel immediate
            try:
                job, method, args, kwargs = self._jobs.get(timeout=max(0.0, next_poll - time.monotonic()))
                try:
                    res = getattr(self.client, method)(*args, **kwargs)
                    self.job_done.emit(job, res, "")
                except Exception as e:  # noqa: BLE001 - shown to the operator
                    self.job_done.emit(job, None, str(e) or type(e).__name__)
                continue
            except queue.Empty:
                pass
            next_poll = time.monotonic() + self.poll_s
            try:
                st: MachineStatus = self.client.read_status()
                self.status.emit(st)
            except Exception as e:  # noqa: BLE001
                self._set_connected(False, str(e))
                try:
                    self.client.close()
                except Exception:  # noqa: BLE001
                    pass
