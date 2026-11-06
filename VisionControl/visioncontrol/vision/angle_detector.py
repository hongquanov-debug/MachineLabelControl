"""
>>> MERGE POINT: camera 1 tab angle algorithm. <<<

Put your algorithm in TabAngleDetector.detect(). Contract (vision/interfaces.py):

    input : Frame (frame.image = numpy uint8), params = settings.camera1.params
            (confidence, angle_offset_deg, roi ...)
    output: AngleResult(quality, theta_deg, reason, overlay)
            - quality VisionQ.OK  -> theta_deg is the COMPENSATION angle the PU wheel
              must turn the can, -180..+180 deg, + = sign agreed with the firmware (M2 DIR)
            - otherwise VisionQ.NO_TAB / DEFECT / WRONG_TYPE, theta_deg None
            - overlay: circles / lines / text to draw (lid circle, tab direction...)
    budget: <= 60 ms (the MCU waits 80 ms after CAM_TRIG)
    never raise for a bad image - return an NG result instead
"""
from __future__ import annotations

from typing import Any

from .interfaces import AngleDetector, AngleResult, Frame


class TabAngleDetector(AngleDetector):
    def __init__(self) -> None:
        # TODO(merge): load model / templates once here
        pass

    def detect(self, frame: Frame, params: dict[str, Any]) -> AngleResult:
        # TODO(merge): replace with the real algorithm
        raise NotImplementedError("Chưa merge thuật toán định góc (vision/angle_detector.py)")
