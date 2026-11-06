"""
DataMatrix decoding for camera 2 (industrial camera + software decoder).

Uses zxing-cpp (pip install zxing-cpp numpy). Input: grayscale or BGR numpy
image from the camera SDK (Hikrobot MVS -> numpy). Output: what was found.

Speed: a full 1280x960 frame takes ~20-50 ms depending on the PC. Set a ROI
(region where the label can be) to cut that several times: camera 2 must
answer within 150 ms (MCU deadline).

Print quality: true ISO/IEC 15415 grading needs a calibrated verifier. Here we
only measure SYMBOL CONTRAST (SC), one of the 15415 parameters, and grade it
with the 15415 SC thresholds (A >= 70 %, B >= 55 %, C >= 40 %, D >= 20 %, else F).
It catches faded / smudged printing, which is the usual failure on a line.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional, Sequence

import numpy as np

try:
    import zxingcpp
except ImportError as e:  # pragma: no cover - reported at first use
    zxingcpp = None
    _IMPORT_ERROR = e

GRADE_LETTER = {4: "A", 3: "B", 2: "C", 1: "D", 0: "F"}


@dataclass
class DecodedSymbol:
    raw: str                         # raw text (GS1: AIs + GS separators)
    is_gs1: bool                     # symbology ]d2 / content type GS1
    valid: bool                      # False = symbol located but not decodable
    corners: list[tuple[int, int]]   # 4 corners in full-image coordinates
    contrast: float = 0.0            # symbol contrast 0..1
    grade: int = 0                   # SC grade 4=A .. 0=F


@dataclass
class ReadResult:
    symbols: list[DecodedSymbol] = field(default_factory=list)
    elapsed_ms: float = 0.0

    @property
    def valid(self) -> list[DecodedSymbol]:
        return [s for s in self.symbols if s.valid]


def contrast_grade(sc: float) -> int:
    """ISO/IEC 15415 symbol contrast grade."""
    return 4 if sc >= 0.70 else 3 if sc >= 0.55 else 2 if sc >= 0.40 else 1 if sc >= 0.20 else 0


def symbol_contrast(gray: np.ndarray, corners: Sequence[tuple[int, int]]) -> float:
    """(light - dark) / 255 inside the symbol bounding box, using 5th/95th percentiles."""
    xs = [p[0] for p in corners]
    ys = [p[1] for p in corners]
    h, w = gray.shape[:2]
    x0, x1 = max(min(xs), 0), min(max(xs) + 1, w)
    y0, y1 = max(min(ys), 0), min(max(ys) + 1, h)
    patch = gray[y0:y1, x0:x1]
    if patch.size < 16:
        return 0.0
    lo, hi = np.percentile(patch, [5, 95])
    return float(hi - lo) / 255.0


class CodeReader:
    """
    reader = CodeReader(roi=(400, 200, 640, 560))   # x, y, w, h of the label area
    result = reader.read(frame)
    """

    def __init__(self, roi: Optional[tuple[int, int, int, int]] = None, try_rotate: bool = True,
                 min_symbol_px: int = 40):
        """
        roi           : (x, y, w, h) label area - faster and fewer false detections
        min_symbol_px : located-but-undecodable "symbols" smaller than this are ignored.
                        Noise can look like a tiny symbol with a checksum error; a real
                        DataMatrix (>= 10x10 modules, several px each) is much larger.
        """
        if zxingcpp is None:
            raise RuntimeError(f"zxing-cpp is not installed: {_IMPORT_ERROR}")
        self.roi = roi
        self.try_rotate = try_rotate
        self.min_symbol_px = min_symbol_px

    def read(self, image: np.ndarray) -> ReadResult:
        import time

        t0 = time.perf_counter()
        gray = image if image.ndim == 2 else image.mean(axis=2).astype(np.uint8)
        ox = oy = 0
        if self.roi:
            x, y, w, h = self.roi
            gray = gray[y:y + h, x:x + w]
            ox, oy = x, y
        found = zxingcpp.read_barcodes(
            np.ascontiguousarray(gray),
            formats=zxingcpp.BarcodeFormat.DataMatrix,
            try_rotate=self.try_rotate,
            text_mode=zxingcpp.TextMode.Plain,     # raw GS1 with GS separators
            return_errors=True,                    # also report located-but-unreadable symbols
        )
        res = ReadResult()
        for b in found:
            p = b.position
            corners = [(pt.x, pt.y) for pt in (p.top_left, p.top_right, p.bottom_right, p.bottom_left)]
            size = max(max(c[0] for c in corners) - min(c[0] for c in corners),
                       max(c[1] for c in corners) - min(c[1] for c in corners))
            if not b.valid and size < self.min_symbol_px:
                continue                           # noise, not a damaged code
            sc = symbol_contrast(gray, corners)
            res.symbols.append(DecodedSymbol(
                raw=b.text if b.valid else "",
                is_gs1=b.valid and (b.content_type == zxingcpp.ContentType.GS1 or b.symbology_identifier == "]d2"),
                valid=b.valid,
                corners=[(cx + ox, cy + oy) for cx, cy in corners],
                contrast=sc,
                grade=contrast_grade(sc),
            ))
        res.elapsed_ms = (time.perf_counter() - t0) * 1000.0
        return res
