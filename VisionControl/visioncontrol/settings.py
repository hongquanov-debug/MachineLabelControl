"""
Application settings, stored as JSON (config/settings.json, created on first run).

Every field has a default so an old settings file keeps working after new
fields are added. Edit through the Configuration page or by hand.
"""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field, fields, is_dataclass
from pathlib import Path
from typing import Any, Optional


@dataclass
class PlcSettings:
    host: str = "192.168.1.200"   # STM32 controller, Modbus TCP (W5500)
    port: int = 502
    poll_ms: int = 250            # read_status period
    timeout_s: float = 2.0


@dataclass
class SerialSettings:
    port: str = "COM3"            # RS485_3 (USB-RS485) to the MCU vision link
    baud: int = 115200            # fixed by the firmware (8N1)


@dataclass
class CameraSettings:
    name: str = ""
    source: str = "sim"           # "sim" or "external" (real camera, see vision/camera_source.py)
    device: str = ""              # camera id / IP for the external source
    roi: Optional[list[int]] = None   # [x, y, w, h] passed to the algorithm
    deadline_ms: int = 80         # MCU waits 80 ms (cam 1) / 150 ms (cam 2)
    save_ng_images: bool = True
    params: dict[str, Any] = field(default_factory=dict)   # algorithm specific


@dataclass
class Settings:
    plc: PlcSettings = field(default_factory=PlcSettings)
    serial: SerialSettings = field(default_factory=SerialSettings)
    camera1: CameraSettings = field(default_factory=lambda: CameraSettings(
        name="Camera 1 · Góc lệch", deadline_ms=80, params={"confidence": 0.72, "angle_offset_deg": 0.0}))
    camera2: CameraSettings = field(default_factory=lambda: CameraSettings(
        name="Camera 2 · DataMatrix", deadline_ms=150,
        params={"gtin": "08935049501237", "min_grade": "C", "duplicate_scope": "shift"}))
    data_dir: str = "data"                       # SQLite history + NG images
    shifts: list[str] = field(default_factory=lambda: ["06:00", "14:00", "22:00"])
    language: str = "vi"
    fullscreen: bool = False


def _merge(cls, data: dict):
    """Build dataclass `cls` from a dict, ignoring unknown keys, keeping defaults."""
    obj = cls()
    for f in fields(cls):
        if f.name not in data:
            continue
        cur = getattr(obj, f.name)
        val = data[f.name]
        setattr(obj, f.name, _merge(type(cur), val) if is_dataclass(cur) and isinstance(val, dict) else val)
    return obj


def load(path: str | Path) -> Settings:
    p = Path(path)
    if not p.exists():
        s = Settings()
        save(s, p)
        return s
    return _merge(Settings, json.loads(p.read_text(encoding="utf-8")))


def save(s: Settings, path: str | Path) -> None:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(asdict(s), ensure_ascii=False, indent=2), encoding="utf-8")
