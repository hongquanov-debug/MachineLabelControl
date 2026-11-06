"""
Simulated camera + algorithms, used with `--sim` (no camera, no controller).
They produce plausible images and results so the whole UI can be developed and
tested. They follow the same interfaces as the real code.
"""
from __future__ import annotations

import math
import random
import time
from typing import Any, Optional

import numpy as np

from ..core.models import NG_COLOR, OK_COLOR, WARN_COLOR, OvCircle, OvLine, OvPolygon, OvText
from ..core.vision_protocol import CodeQ, VisionQ
from .interfaces import AngleDetector, AngleResult, CodeReader, CodeResult, Frame, FrameSource

W, H = 640, 480
_yy, _xx = np.mgrid[0:H, 0:W].astype(np.float32)


def _lid(cx: float, cy: float, r: float) -> np.ndarray:
    img = np.full((H, W), 12, np.float32)
    d = np.hypot(_xx - cx, _yy - cy)
    inside = d <= r
    img[inside] = 200 - 70 * (d[inside] / r)               # radial shading
    ring = (d > r * 0.86) & (d < r * 0.92)
    img[ring] = 165
    return img


class SimFrameSource(FrameSource):
    def __init__(self, camera: int, seed: Optional[int] = None):
        self.camera = camera
        self.rng = random.Random(seed)
        self.serial = 0x3A7F21

    def open(self) -> None: ...

    def close(self) -> None: ...

    def grab(self, timeout_ms: int) -> Optional[Frame]:
        t = time.perf_counter() * 1000.0
        cx, cy, r = W / 2 + self.rng.uniform(-8, 8), H / 2 + self.rng.uniform(-8, 8), 190.0
        img = _lid(cx, cy, r)
        meta: dict[str, Any] = {"cx": cx, "cy": cy, "r": r}
        if self.camera == 1:
            theta = self.rng.uniform(-180, 180)
            has_tab = self.rng.random() > 0.03
            if has_tab:
                a = math.radians(theta)
                ux, uy = math.cos(a), math.sin(a)
                px, py = _xx - cx, _yy - cy
                u = px * ux + py * uy
                v = -px * uy + py * ux
                tab = ((u - r * 0.38) / (r * 0.34)) ** 2 + (v / (r * 0.2)) ** 2 <= 1
                hole = ((u - r * 0.5) / (r * 0.14)) ** 2 + (v / (r * 0.09)) ** 2 <= 1
                img[tab] = 175
                img[hole] = 55
            meta.update(theta=theta, has_tab=has_tab)
        else:
            roll = self.rng.random()
            outcome = ("NO_CODE" if roll < 0.01 else "UNREADABLE" if roll < 0.022 else
                       "DUPLICATE" if roll < 0.027 else "LOW_GRADE" if roll < 0.032 else "OK")
            if outcome != "NO_CODE":
                img[int(cy - 70):int(cy + 70), int(cx - 120):int(cx + 120)] = 238      # label
                n, m = 16, 7
                x0, y0 = int(cx + 20), int(cy - n * m / 2)
                bits = np.array([[self.rng.random() > 0.5 for _ in range(n)] for _ in range(n)])
                bits[:, 0] = True
                bits[n - 1, :] = True
                bits[0, ::2] = True
                bits[1::2, n - 1] = True
                dark = 140 if outcome == "LOW_GRADE" else 20
                for i in range(n):
                    for j in range(n):
                        if bits[i, j]:
                            img[y0 + i * m:y0 + (i + 1) * m, x0 + j * m:x0 + (j + 1) * m] = dark
                if outcome == "UNREADABLE":
                    img[y0 + 30:y0 + 80, x0 + 10:x0 + 100] = 238
                meta["box"] = (x0, y0, n * m)
            self.serial += 1
            meta.update(outcome=outcome, serial=f"{self.serial:08X}")
        noise = np.random.default_rng(self.rng.randrange(1 << 30)).normal(0, 4, (H, W))
        img = np.clip(img + noise, 0, 255).astype(np.uint8)
        return Frame(img, self.camera, t, meta)


class SimAngleDetector(AngleDetector):
    def detect(self, frame: Frame, params: dict[str, Any]) -> AngleResult:
        m = frame.meta
        time.sleep(random.uniform(0.012, 0.035))              # pretend to work
        cx, cy, r = m["cx"], m["cy"], m["r"]
        if not m.get("has_tab"):
            return AngleResult(VisionQ.NO_TAB, None, "không thấy khoen",
                               [OvCircle(cx, cy, r, NG_COLOR, dashed=True)])
        th = m["theta"] + float(params.get("angle_offset_deg", 0.0))
        a = math.radians(m["theta"])
        ov = [OvCircle(cx, cy, r, OK_COLOR, dashed=True),
              OvLine(cx, cy, cx + math.cos(a) * r * 1.1, cy + math.sin(a) * r * 1.1, OK_COLOR, 3),
              OvLine(cx - r * 1.15, cy, cx + r * 1.15, cy, "#ffffff55", 1),
              OvLine(cx, cy - r * 1.15, cx, cy + r * 1.15, "#ffffff55", 1)]
        return AngleResult(VisionQ.OK, max(-180.0, min(180.0, th)), "", ov)


class SimCodeReader(CodeReader):
    def read(self, frame: Frame, params: dict[str, Any]) -> CodeResult:
        m = frame.meta
        time.sleep(random.uniform(0.025, 0.07))
        outcome = m["outcome"]
        q = CodeQ[outcome]
        gtin = params.get("gtin", "08935049501237")
        data = "" if outcome in ("NO_CODE", "UNREADABLE") else f"(01){gtin}(21){m['serial']}(17)271231(10)L2611"
        grade = None if not data else (1 if outcome == "LOW_GRADE" else random.choice([2, 3, 4]))
        ov = []
        if "box" in m:
            x0, y0, s = m["box"]
            col = OK_COLOR if q == CodeQ.OK else WARN_COLOR if data else NG_COLOR
            ov.append(OvPolygon([(x0 - 4, y0 - 4), (x0 + s + 4, y0 - 4), (x0 + s + 4, y0 + s + 4), (x0 - 4, y0 + s + 4)],
                                col, dashed=not data))
        ov.append(OvText(10, H - 12, data or {"NO_CODE": "KHÔNG THẤY MÃ", "UNREADABLE": "KHÔNG GIẢI MÃ ĐƯỢC"}[outcome],
                         OK_COLOR if q == CodeQ.OK else NG_COLOR))
        reason = {"OK": "", "NO_CODE": "không thấy mã", "UNREADABLE": "không giải được",
                  "DUPLICATE": "trùng serial", "LOW_GRADE": "in mờ"}[outcome]
        return CodeResult(q, data, grade, reason, ov)
