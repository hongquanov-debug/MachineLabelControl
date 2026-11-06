"""
>>> MERGE POINT: camera 2 DataMatrix reading algorithm. <<<

Put your algorithm in DataMatrixReader.read(). Contract (vision/interfaces.py):

    input : Frame, params = settings.camera2.params (gtin of the recipe, min_grade,
            duplicate_scope, roi ...)
    output: CodeResult(quality, data, grade, reason, overlay)
            - CodeQ.OK only when the code is found, decoded and its content accepted
            - NO_CODE / UNREADABLE / INVALID / DUPLICATE / LOW_GRADE otherwise
              (the MCU rejects every can that is not OK)
            - data: decoded text, stored in the PC history (traceability), not sent to the MCU
            - grade: 4=A .. 0=F or None if not measured
    budget: <= 110 ms (the MCU waits 150 ms after CAM2_TRIG)
    never raise for a bad image - return an NG result instead
"""
from __future__ import annotations

from typing import Any

from .interfaces import CodeReader, CodeResult, Frame


class DataMatrixReader(CodeReader):
    def __init__(self) -> None:
        # TODO(merge): init decoder, duplicate store ...
        pass

    def read(self, frame: Frame, params: dict[str, Any]) -> CodeResult:
        # TODO(merge): replace with the real algorithm
        raise NotImplementedError("Chưa merge thuật toán đọc DataMatrix (vision/code_reader.py)")
