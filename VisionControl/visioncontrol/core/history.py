"""
SQLite history: event log, production samples (for reports) and vision results.

Production reports: the firmware only has total counters (30004..30009,
30025..30028). The app samples them every minute and stores the DELTA, so
hourly / shift / daily reports are sums over `production`.
"""
from __future__ import annotations

import sqlite3
import threading
from datetime import datetime
from pathlib import Path
from typing import Optional

from .models import LogEntry, VisionRecord

SCHEMA = """
CREATE TABLE IF NOT EXISTS events (
    ts TEXT, level TEXT, category TEXT, source TEXT, code TEXT, message TEXT);
CREATE INDEX IF NOT EXISTS ix_events_ts ON events(ts);
CREATE TABLE IF NOT EXISTS production (
    ts TEXT, recipe INTEGER, fed INTEGER, ok INTEGER, ng INTEGER, code_ok INTEGER, code_ng INTEGER,
    run_s REAL);
CREATE INDEX IF NOT EXISTS ix_prod_ts ON production(ts);
CREATE TABLE IF NOT EXISTS vision (
    ts TEXT, camera INTEGER, can_id INTEGER, quality INTEGER, quality_name TEXT, ok INTEGER,
    theta REAL, data TEXT, grade INTEGER, elapsed_ms REAL, late INTEGER, reason TEXT, image_path TEXT);
CREATE INDEX IF NOT EXISTS ix_vision_ts ON vision(ts);
"""

ISO = "%Y-%m-%d %H:%M:%S"


class HistoryDB:
    def __init__(self, path: str | Path = ":memory:"):
        if str(path) != ":memory:":
            Path(path).parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self.db = sqlite3.connect(str(path), check_same_thread=False)
        self.db.executescript(SCHEMA)

    # ---------------------------------------------------------------- write
    def add_event(self, e: LogEntry) -> None:
        with self._lock:
            self.db.execute("INSERT INTO events VALUES (?,?,?,?,?,?)",
                            (e.ts.strftime(ISO), e.level, e.category, e.source, e.code, e.message))
            self.db.commit()

    def add_production(self, ts: datetime, recipe: int, fed: int, ok: int, ng: int,
                       code_ok: int, code_ng: int, run_s: float) -> None:
        with self._lock:
            self.db.execute("INSERT INTO production VALUES (?,?,?,?,?,?,?,?)",
                            (ts.strftime(ISO), recipe, fed, ok, ng, code_ok, code_ng, run_s))
            self.db.commit()

    def add_vision(self, r: VisionRecord, image_path: str = "") -> None:
        with self._lock:
            self.db.execute("INSERT INTO vision VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                            (r.ts.strftime(ISO), r.camera, r.can_id, r.quality, r.quality_name, int(r.ok),
                             r.theta_deg, r.data, r.grade, round(r.elapsed_ms, 1), int(r.late), r.reason, image_path))
            self.db.commit()

    # ---------------------------------------------------------------- read
    def events(self, since: Optional[datetime] = None, limit: int = 2000) -> list[LogEntry]:
        q, a = "SELECT ts, level, category, source, code, message FROM events", []
        if since:
            q += " WHERE ts >= ?"
            a.append(since.strftime(ISO))
        q += " ORDER BY rowid DESC LIMIT ?"
        a.append(limit)
        with self._lock:
            rows = self.db.execute(q, a).fetchall()
        return [LogEntry(level=r[1], category=r[2], source=r[3], code=r[4], message=r[5],
                         ts=datetime.strptime(r[0], ISO)) for r in rows]

    def hourly(self, start: datetime, end: datetime, recipe: Optional[int] = None) -> list[tuple[str, int, int, int]]:
        """[(YYYY-MM-DD HH, fed, ok, ng)] between start (incl.) and end (excl.)."""
        q = ("SELECT substr(ts,1,13) h, SUM(fed), SUM(ok), SUM(ng) FROM production "
             "WHERE ts >= ? AND ts < ?")
        a: list = [start.strftime(ISO), end.strftime(ISO)]
        if recipe:
            q += " AND recipe = ?"
            a.append(recipe)
        q += " GROUP BY h ORDER BY h"
        with self._lock:
            return [(r[0], r[1] or 0, r[2] or 0, r[3] or 0) for r in self.db.execute(q, a)]

    def totals(self, start: datetime, end: datetime, recipe: Optional[int] = None) -> dict[str, float]:
        q = ("SELECT SUM(fed), SUM(ok), SUM(ng), SUM(code_ok), SUM(code_ng), SUM(run_s) FROM production "
             "WHERE ts >= ? AND ts < ?")
        a: list = [start.strftime(ISO), end.strftime(ISO)]
        if recipe:
            q += " AND recipe = ?"
            a.append(recipe)
        with self._lock:
            r = self.db.execute(q, a).fetchone()
        keys = ("fed", "ok", "ng", "code_ok", "code_ng", "run_s")
        return {k: (v or 0) for k, v in zip(keys, r)}

    def ng_reasons(self, start: datetime, end: datetime) -> list[tuple[str, int]]:
        """NG causes from vision results: [(label, count)] most frequent first."""
        with self._lock:
            rows = self.db.execute(
                "SELECT camera, quality_name, COUNT(*) FROM vision WHERE ok = 0 AND ts >= ? AND ts < ? "
                "GROUP BY camera, quality_name ORDER BY 3 DESC",
                (start.strftime(ISO), end.strftime(ISO))).fetchall()
        return [(f"Cam{c}: {name}", n) for c, name, n in rows]

    def alarm_counts(self, start: datetime, end: datetime) -> list[tuple[str, str, int]]:
        with self._lock:
            rows = self.db.execute(
                "SELECT code, message, COUNT(*) FROM events WHERE category = 'alarm' AND level = 'err' "
                "AND ts >= ? AND ts < ? GROUP BY code ORDER BY 3 DESC",
                (start.strftime(ISO), end.strftime(ISO))).fetchall()
        return [(r[0], r[1], r[2]) for r in rows]
