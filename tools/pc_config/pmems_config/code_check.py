"""
Camera 2 decision logic: decoded symbols -> QUALITY_FLAG for the 0x21 frame.

    rules = CodeRules(gtin="08935049501237")          # from the active recipe
    checker = CodeChecker(rules, DuplicateStore("D:/PMeMS/data/serials.db"))
    verdict = checker.check(reader.read(frame))
    port.write(result_cam2(can_id, verdict.quality, verdict.grade, verdict.data_len))

Order of the checks (first failure wins):
    NO_CODE     no DataMatrix located in the image (label missing / wrong place)
    UNREADABLE  located but not decodable (damaged, blurred, smudged)
    INVALID     decoded but content rejected: not GS1, unknown/bad AI, wrong GTIN,
                missing required AI, length out of range, expired, several codes
    LOW_GRADE   decoded and valid, symbol contrast grade below the minimum
    DUPLICATE   serial (AI 21) already produced in the configured scope
    OK          everything fine -> serial is registered as used
"""
from __future__ import annotations

import sqlite3
import threading
from dataclasses import dataclass, field
from datetime import date
from typing import Optional

from . import gs1
from .code_reader import GRADE_LETTER, ReadResult
from .vision_link import CodeQ

GRADE_NOT_MEASURED = 0xFF


@dataclass
class CodeRules:
    gtin: Optional[str] = None                 # expected AI 01 (from recipe); None = any
    require_gs1: bool = True
    required_ais: tuple[str, ...] = ("01", "21")
    min_len: int = 16                          # raw data length
    max_len: int = 80
    reject_expired: bool = True                # AI 17 earlier than today -> INVALID
    min_grade: Optional[int] = 2               # symbol contrast grade (2 = C); None = no check
    check_duplicates: bool = True


class DuplicateStore:
    """
    Serial numbers already produced. In memory, optionally persisted in SQLite so
    duplicates are also caught after a PC restart. Thread-safe.
    Scope ("trong ca", "trong ngày"...) = when you call clear().
    """

    def __init__(self, sqlite_path: Optional[str] = None):
        self._lock = threading.Lock()
        self._mem: set[str] = set()
        self._db = None
        if sqlite_path:
            self._db = sqlite3.connect(sqlite_path, check_same_thread=False)
            self._db.execute("CREATE TABLE IF NOT EXISTS serials (key TEXT PRIMARY KEY, ts TEXT DEFAULT CURRENT_TIMESTAMP)")
            self._mem.update(r[0] for r in self._db.execute("SELECT key FROM serials"))

    def seen(self, key: str) -> bool:
        with self._lock:
            return key in self._mem

    def add(self, key: str) -> None:
        with self._lock:
            self._mem.add(key)
            if self._db:
                self._db.execute("INSERT OR IGNORE INTO serials(key) VALUES (?)", (key,))
                self._db.commit()

    def clear(self) -> None:
        with self._lock:
            self._mem.clear()
            if self._db:
                self._db.execute("DELETE FROM serials")
                self._db.commit()


@dataclass
class CodeVerdict:
    quality: CodeQ
    reason: str = ""
    raw: str = ""
    fields: dict[str, str] = field(default_factory=dict)
    grade: int = GRADE_NOT_MEASURED
    contrast: float = 0.0
    corners: list[tuple[int, int]] = field(default_factory=list)

    @property
    def data_len(self) -> int:
        return len(self.raw)

    @property
    def hri(self) -> str:
        return gs1.to_hri(self.fields) if self.fields else self.raw

    @property
    def grade_letter(self) -> str:
        return GRADE_LETTER.get(self.grade, "–")


class CodeChecker:
    def __init__(self, rules: CodeRules, store: Optional[DuplicateStore] = None):
        self.rules = rules
        self.store = store or DuplicateStore()

    def check(self, result: ReadResult, today: Optional[date] = None) -> CodeVerdict:
        r = self.rules
        if not result.symbols:
            return CodeVerdict(CodeQ.NO_CODE, "không thấy mã DataMatrix")
        valid = result.valid
        if not valid:
            s = result.symbols[0]
            return CodeVerdict(CodeQ.UNREADABLE, "thấy mã nhưng không giải được",
                               grade=s.grade, contrast=s.contrast, corners=s.corners)
        if len({s.raw for s in valid}) > 1:
            return CodeVerdict(CodeQ.INVALID, f"{len(valid)} mã khác nhau trong ảnh", raw=valid[0].raw)
        s = valid[0]
        v = CodeVerdict(CodeQ.INVALID, raw=s.raw, grade=s.grade, contrast=s.contrast, corners=s.corners)

        if not r.min_len <= len(s.raw) <= r.max_len:
            v.reason = f"độ dài {len(s.raw)} ngoài {r.min_len}..{r.max_len}"
            return v
        if r.require_gs1 and not s.is_gs1:
            v.reason = "không phải GS1 DataMatrix"
            return v
        if s.is_gs1 or r.require_gs1:
            try:
                v.fields = gs1.parse(s.raw)
            except gs1.GS1Error as e:
                v.reason = f"GS1 lỗi: {e}"
                return v
            missing = [ai for ai in r.required_ais if ai not in v.fields]
            if missing:
                v.reason = "thiếu AI " + ", ".join(missing)
                return v
            if r.gtin and v.fields.get("01") != r.gtin:
                v.reason = f"sai GTIN {v.fields.get('01')} (cần {r.gtin})"
                return v
            if r.reject_expired and "17" in v.fields:
                if gs1.parse_date(v.fields["17"]) < (today or date.today()):
                    v.reason = f"hết hạn {v.fields['17']}"
                    return v
        if r.min_grade is not None and s.grade < r.min_grade:
            v.quality = CodeQ.LOW_GRADE
            v.reason = f"độ tương phản {s.contrast:.0%} (grade {GRADE_LETTER[s.grade]})"
            return v
        key = (v.fields.get("01", "") + "/" + v.fields["21"]) if "21" in v.fields else s.raw
        if r.check_duplicates and self.store.seen(key):
            v.quality = CodeQ.DUPLICATE
            v.reason = "mã đã xuất hiện trước đó"
            return v
        if r.check_duplicates:
            self.store.add(key)       # only accepted codes consume their serial
        v.quality = CodeQ.OK
        return v
