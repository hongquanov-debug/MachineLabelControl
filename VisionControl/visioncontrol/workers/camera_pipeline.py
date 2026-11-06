"""
One pipeline per camera, in its own thread:

    trigger (CAN_ID) -> FrameSource.grab() -> AngleDetector / CodeReader
                     -> result frame 0x20 / 0x21 -> link.write()  (MCU)
                     -> VisionRecord -> `result` callback          (UI, history)

Rules
* The result frame is sent FIRST, then the UI / history work happens.
* An exception in the algorithm never stops the pipeline: the can gets an NG
  result (DEFECT / UNREADABLE) and the error is reported in the record reason.
* Results later than the MCU deadline are still sent (the MCU ignores them) and
  are flagged `late` so the operator can see the PC is too slow.
"""
from __future__ import annotations

import queue
import threading
import time
from typing import Any, Callable, Optional

from ..core.models import VisionRecord
from ..core.vision_protocol import CodeQ, VisionQ, result_cam1, result_cam2
from ..vision.interfaces import AngleDetector, CodeReader, FrameSource


class CameraPipeline:
    def __init__(self, camera: int, source: FrameSource, algorithm: AngleDetector | CodeReader,
                 link: Any, params: Callable[[], dict[str, Any]], deadline_ms: float,
                 on_result: Callable[[VisionRecord], None]):
        self.camera = camera
        self.source = source
        self.algorithm = algorithm
        self.link = link
        self.params = params               # called for each can (recipe / settings may change)
        self.deadline_ms = deadline_ms
        self.on_result = on_result
        self._q: "queue.Queue[tuple[int, float]]" = queue.Queue(maxsize=16)
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self.error: str = ""

    # called from the link thread
    def trigger(self, can_id: int, t_ms: float) -> None:
        try:
            self._q.put_nowait((can_id, t_ms))
        except queue.Full:                 # PC far too slow: drop, the MCU will time out
            pass

    def start(self) -> None:
        try:
            self.source.open()
        except Exception as e:  # noqa: BLE001
            self.error = str(e)
        self._running = True
        self._thread = threading.Thread(target=self._loop, name=f"Camera{self.camera}", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)
        try:
            self.source.close()
        except Exception:  # noqa: BLE001
            pass

    def _loop(self) -> None:
        while self._running:
            try:
                can_id, t_trig = self._q.get(timeout=0.2)
            except queue.Empty:
                continue
            self._process(can_id, t_trig)

    def _process(self, can_id: int, t_trig: float) -> None:
        frame, image, overlay, reason = None, None, [], ""
        try:
            frame = self.source.grab(int(self.deadline_ms))
            if frame is not None:
                image = frame.image
        except Exception as e:  # noqa: BLE001
            reason = f"lỗi camera: {e}"
        try:
            params = self.params()
            if self.camera == 1:
                if frame is None:
                    q, theta = VisionQ.DEFECT, None
                    reason = reason or "không có ảnh"
                else:
                    r = self.algorithm.detect(frame, params)
                    q, theta, reason, overlay = r.quality, r.theta_deg, r.reason, r.overlay
                ok = q == VisionQ.OK and theta is not None
                payload = result_cam1(can_id, theta or 0.0, quality=q if not ok else None)
                rec = VisionRecord(1, can_id, int(q), VisionQ(q).name, ok, 0.0, theta_deg=theta if ok else None)
            else:
                if frame is None:
                    cq, data, grade = CodeQ.NO_CODE, "", None
                    reason = reason or "không có ảnh"
                else:
                    r = self.algorithm.read(frame, params)
                    cq, data, grade, reason, overlay = r.quality, r.data, r.grade, r.reason, r.overlay
                payload = result_cam2(can_id, cq, 0xFF if grade is None else grade, len(data))
                rec = VisionRecord(2, can_id, int(cq), CodeQ(cq).name, cq == CodeQ.OK, 0.0, data=data, grade=grade)
        except Exception as e:  # noqa: BLE001 - algorithm bug must not stop production
            reason = f"lỗi thuật toán: {e}"
            if self.camera == 1:
                payload = result_cam1(can_id, 0.0, quality=VisionQ.DEFECT)
                rec = VisionRecord(1, can_id, int(VisionQ.DEFECT), "DEFECT", False, 0.0)
            else:
                payload = result_cam2(can_id, CodeQ.UNREADABLE)
                rec = VisionRecord(2, can_id, int(CodeQ.UNREADABLE), "UNREADABLE", False, 0.0)

        self.link.write(payload)                                   # MCU first
        rec.elapsed_ms = time.perf_counter() * 1000.0 - t_trig
        rec.late = rec.elapsed_ms > self.deadline_ms
        rec.reason, rec.image, rec.overlay = reason, image, overlay
        self.on_result(rec)
