"""Shared data structures (no Qt, no I/O)."""
from __future__ import annotations

import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional

import numpy as np

# ------------------------------------------------------------------ overlay
# Algorithms describe what to draw on the image with these primitives;
# the UI (ImageView) draws them. Coordinates are image pixels.
OK_COLOR, WARN_COLOR, NG_COLOR, INFO_COLOR = "#22c55e", "#f5a524", "#ef4444", "#38bdf8"


@dataclass
class OvCircle:
    x: float
    y: float
    r: float
    color: str = OK_COLOR
    dashed: bool = False


@dataclass
class OvLine:
    x1: float
    y1: float
    x2: float
    y2: float
    color: str = OK_COLOR
    width: float = 2.0


@dataclass
class OvPolygon:
    points: list[tuple[float, float]]
    color: str = OK_COLOR
    dashed: bool = False


@dataclass
class OvText:
    x: float
    y: float
    text: str
    color: str = OK_COLOR


Overlay = list  # list[OvCircle | OvLine | OvPolygon | OvText]


# ------------------------------------------------------------------ vision
@dataclass
class VisionRecord:
    """One camera result as shown in the UI and stored in the history DB."""
    camera: int                       # 1 = tab angle, 2 = DataMatrix
    can_id: int
    quality: int                      # QUALITY_FLAG sent to the MCU
    quality_name: str
    ok: bool
    elapsed_ms: float                 # trigger -> result frame sent
    late: bool = False                # elapsed > MCU deadline (MCU already rejected the can)
    theta_deg: Optional[float] = None # camera 1
    data: str = ""                    # camera 2 decoded text (stays on the PC)
    grade: Optional[int] = None       # camera 2: 4=A .. 0=F, None = not measured
    reason: str = ""
    image: Optional[np.ndarray] = None
    overlay: Overlay = field(default_factory=list)
    ts: datetime = field(default_factory=datetime.now)


# ------------------------------------------------------------------ log
LEVEL_INFO, LEVEL_WARN, LEVEL_ERR = "info", "warn", "err"
CAT_ALARM, CAT_EVENT, CAT_VISION, CAT_COMM = "alarm", "event", "vision", "comm"


@dataclass
class LogEntry:
    level: str
    category: str
    source: str
    message: str
    code: str = ""
    ts: datetime = field(default_factory=datetime.now)


def now_ms() -> float:
    return time.perf_counter() * 1000.0
