"""
Camera 2 station on the Vision PC: frame -> decode -> check -> 0x21 frame + traceability log.

Typical integration (one thread per camera, ONE writer for the serial port):

    station = Camera2Station(CodeReader(roi=(400, 200, 640, 560)),
                             CodeChecker(CodeRules(gtin=recipe_gtin), DuplicateStore("serials.db")),
                             TraceLog("trace.db"))

    # trigger 0x11 received from the MCU (TriggerParser) + frame grabbed on CAM2_TRIG:
    frame_bytes, verdict = station.process(can_id, frame)
    serial_writer.put(frame_bytes)          # queue drained by the single RS485 writer thread
    gui_signal.emit(can_id, verdict)        # update the Camera 2 page (PyQt6 signal)

Budget: the MCU waits 150 ms after CAM2_TRIG. Keep decode + check under ~110 ms
(use a ROI); `process()` reports the elapsed time so you can monitor it.
"""
from __future__ import annotations

import sqlite3
import threading
import time
from typing import Optional

import numpy as np

from .code_check import CodeChecker, CodeVerdict
from .code_reader import CodeReader
from .vision_link import result_cam2

DEADLINE_MS = 150.0


class TraceLog:
    """Every camera 2 reading, for traceability and reports (SQLite)."""

    def __init__(self, path: str = ":memory:"):
        self._lock = threading.Lock()
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.execute("""CREATE TABLE IF NOT EXISTS code_log (
            ts TEXT DEFAULT (strftime('%Y-%m-%d %H:%M:%f','now','localtime')),
            can_id INTEGER, quality INTEGER, reason TEXT, data TEXT,
            gtin TEXT, serial TEXT, lot TEXT, expiry TEXT,
            grade INTEGER, contrast REAL, elapsed_ms REAL, recipe INTEGER)""")
        self.db.commit()

    def add(self, can_id: int, v: CodeVerdict, elapsed_ms: float, recipe: Optional[int] = None) -> None:
        f = v.fields
        with self._lock:
            self.db.execute(
                "INSERT INTO code_log(can_id, quality, reason, data, gtin, serial, lot, expiry, grade, contrast, elapsed_ms, recipe)"
                " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
                (can_id, int(v.quality), v.reason, v.hri, f.get("01"), f.get("21"), f.get("10"), f.get("17"),
                 v.grade, round(v.contrast, 3), round(elapsed_ms, 1), recipe))
            self.db.commit()

    def recent(self, n: int = 50) -> list[tuple]:
        with self._lock:
            return self.db.execute("SELECT ts, can_id, quality, data, grade, elapsed_ms FROM code_log "
                                   "ORDER BY rowid DESC LIMIT ?", (n,)).fetchall()


class Camera2Station:
    def __init__(self, reader: CodeReader, checker: CodeChecker,
                 trace: Optional[TraceLog] = None, recipe_id: Optional[int] = None):
        self.reader = reader
        self.checker = checker
        self.trace = trace
        self.recipe_id = recipe_id
        self.late = 0                               # results computed after the MCU deadline

    def process(self, can_id: int, frame: np.ndarray, t_trigger: Optional[float] = None):
        """
        Returns (frame_bytes_for_MCU, verdict). `t_trigger` = time.perf_counter() when the
        image was taken; used to warn when the 150 ms MCU window is exceeded.
        """
        t0 = t_trigger if t_trigger is not None else time.perf_counter()
        result = self.reader.read(frame)
        verdict = self.checker.check(result)
        elapsed = (time.perf_counter() - t0) * 1000.0
        if elapsed > DEADLINE_MS:
            self.late += 1                          # the MCU has already rejected this can
        if self.trace:
            self.trace.add(can_id, verdict, elapsed, self.recipe_id)
        frame_bytes = result_cam2(can_id, verdict.quality, verdict.grade, verdict.data_len)
        return frame_bytes, verdict
