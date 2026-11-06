"""Factories: choose the simulated or the real implementation from the settings."""
from __future__ import annotations

from ..settings import CameraSettings
from .interfaces import AngleDetector, CodeReader, FrameSource


def make_source(camera: int, cfg: CameraSettings) -> FrameSource:
    if cfg.source == "sim":
        from .sim import SimFrameSource
        return SimFrameSource(camera)
    from .camera_source import ExternalCameraSource
    return ExternalCameraSource(camera, cfg.device, cfg.roi)


def make_angle_detector(cfg: CameraSettings) -> AngleDetector:
    if cfg.source == "sim":
        from .sim import SimAngleDetector
        return SimAngleDetector()
    from .angle_detector import TabAngleDetector
    return TabAngleDetector()


def make_code_reader(cfg: CameraSettings) -> CodeReader:
    if cfg.source == "sim":
        from .sim import SimCodeReader
        return SimCodeReader()
    from .code_reader import DataMatrixReader
    return DataMatrixReader()
