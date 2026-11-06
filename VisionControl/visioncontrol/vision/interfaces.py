"""
CONTRACTS between the application and the vision code.

The application never calls a camera SDK or an image algorithm directly; it
only uses these three interfaces. To plug in the real code, implement them in

    vision/camera_source.py   -> ExternalCameraSource(FrameSource)
    vision/angle_detector.py  -> TabAngleDetector(AngleDetector)      camera 1
    vision/code_reader.py     -> DataMatrixReader(CodeReader)         camera 2

and keep the signatures below. Everything else (timing, RS485 frames to the MCU,
UI, history, NG images) is handled by workers/camera_pipeline.py.

Threading: each camera runs in its own worker thread. `grab()` and
`detect()` / `read()` of one camera are always called from that thread, one
can at a time. They must not touch Qt widgets.

Timing: the MCU waits 80 ms (camera 1) / 150 ms (camera 2) after the
hardware trigger. Budget for grab + algorithm: <= 60 ms / <= 110 ms.
"""
from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any, Optional

import numpy as np

from ..core.models import Overlay
from ..core.vision_protocol import CodeQ, VisionQ


@dataclass
class Frame:
    image: np.ndarray                 # HxW uint8 (gray) or HxWx3 uint8 (BGR)
    camera: int                       # 1 or 2
    t_capture_ms: float               # time.perf_counter() * 1000 at exposure
    meta: dict[str, Any] = field(default_factory=dict)   # free (sim ground truth, exposure...)


class FrameSource(ABC):
    """A camera. Hardware trigger: CAM_TRIG (PD14) for camera 1, CAM2_TRIG (PD15) for camera 2."""

    @abstractmethod
    def open(self) -> None: ...

    @abstractmethod
    def close(self) -> None: ...

    @abstractmethod
    def grab(self, timeout_ms: int) -> Optional[Frame]:
        """Return the frame of the LAST hardware trigger, or None on timeout."""


# ------------------------------------------------------------------ camera 1
@dataclass
class AngleResult:
    quality: VisionQ                  # OK / NO_TAB / DEFECT / WRONG_TYPE  -> QUALITY_FLAG of 0x20
    theta_deg: Optional[float] = None # compensation angle -180..+180 (required when OK)
    reason: str = ""                  # short text for the operator / log
    overlay: Overlay = field(default_factory=list)   # what to draw on the image (core/models.py)


class AngleDetector(ABC):
    @abstractmethod
    def detect(self, frame: Frame, params: dict[str, Any]) -> AngleResult:
        """params = settings.camera1.params (+ 'roi'). Must not raise for a bad image: return NG."""


# ------------------------------------------------------------------ camera 2
@dataclass
class CodeResult:
    quality: CodeQ                    # OK / NO_CODE / UNREADABLE / INVALID / DUPLICATE / LOW_GRADE -> 0x21
    data: str = ""                    # decoded content (kept on the PC, never sent to the MCU)
    grade: Optional[int] = None       # 4=A .. 0=F, None = not measured (sent as 0xFF)
    reason: str = ""
    overlay: Overlay = field(default_factory=list)


class CodeReader(ABC):
    @abstractmethod
    def read(self, frame: Frame, params: dict[str, Any]) -> CodeResult:
        """params = settings.camera2.params (+ 'roi', 'gtin' of the active recipe...)."""
