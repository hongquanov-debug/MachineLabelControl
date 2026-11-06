"""
>>> MERGE POINT: real camera acquisition (from your camera project). <<<

Replace the body of ExternalCameraSource with your camera SDK code (e.g. Hikrobot
MVS). Keep the class name and the FrameSource interface (vision/interfaces.py).
Selected when settings.cameraN.source == "external".
"""
from __future__ import annotations

from typing import Optional

from .interfaces import Frame, FrameSource


class ExternalCameraSource(FrameSource):
    def __init__(self, camera: int, device: str, roi: Optional[list[int]] = None):
        self.camera = camera
        self.device = device       # serial number / IP from settings.cameraN.device
        self.roi = roi

    def open(self) -> None:
        # TODO(merge): open the camera, hardware trigger mode (line input), start grabbing
        raise NotImplementedError(
            f"Camera {self.camera}: chưa merge code kết nối camera (vision/camera_source.py)")

    def close(self) -> None:
        # TODO(merge): stop grabbing, close the device
        pass

    def grab(self, timeout_ms: int) -> Optional[Frame]:
        # TODO(merge): wait for the frame of the last trigger, convert to numpy (gray uint8),
        #              return Frame(image, self.camera, t_capture_ms=time.perf_counter()*1000)
        raise NotImplementedError
