"""
Approximate can positions for the conveyor drawing.

The firmware does not publish individual can positions. The PC knows when
each can passed camera 1 (trigger 0x10 at +50 mm) and camera 2 (0x11 at
+1050 mm), and the belt speed (status.speed_cans_min x 120 mm pitch). That is
enough to draw the cans moving and coloured by their vision results.
Display only - never use it for control.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

POS_CAM1, POS_ORIENT, POS_LABEL, POS_S5, POS_CAM2, POS_REJECT, POS_END = 50, 300, 650, 950, 1050, 1250, 2000
PITCH_MM = 120.0

ST_WAIT, ST_ANGLE_OK, ST_LABELED, ST_NG, ST_PASS = "wait", "angle_ok", "labeled", "ng", "pass"


@dataclass
class TrackedCan:
    can_id: int
    pos_mm: float
    state: str = ST_WAIT
    rejected: bool = False


class CanTracker:
    def __init__(self) -> None:
        self.cans: dict[int, TrackedCan] = {}

    @staticmethod
    def speed_mm_s(cans_per_min: int) -> float:
        return cans_per_min * PITCH_MM / 60.0

    def on_trigger(self, camera: int, can_id: int) -> None:
        c = self.cans.get(can_id)
        if camera == 1:
            self.cans[can_id] = TrackedCan(can_id, POS_CAM1)
        elif c is not None:
            c.pos_mm = POS_CAM2                       # re-synchronise at camera 2
        else:
            self.cans[can_id] = TrackedCan(can_id, POS_CAM2, ST_LABELED)

    def on_result(self, camera: int, can_id: int, ok: bool) -> None:
        c = self.cans.get(can_id)
        if c is None:
            return
        if not ok:
            c.state = ST_NG
        elif camera == 1:
            c.state = ST_ANGLE_OK
        else:
            c.state = ST_PASS

    def advance(self, dt_s: float, cans_per_min: int) -> None:
        v = self.speed_mm_s(cans_per_min)
        for c in list(self.cans.values()):
            c.pos_mm += v * dt_s
            if c.state == ST_ANGLE_OK and c.pos_mm >= POS_LABEL:
                c.state = ST_LABELED
            if c.state == ST_NG and c.pos_mm >= POS_REJECT:
                c.rejected = True
            if c.pos_mm >= POS_END or (c.rejected and c.pos_mm >= POS_REJECT + 60):
                del self.cans[c.can_id]

    def clear(self) -> None:
        self.cans.clear()

    def snapshot(self) -> list[TrackedCan]:
        return sorted(self.cans.values(), key=lambda c: c.pos_mm)

    def get(self, can_id: int) -> Optional[TrackedCan]:
        return self.cans.get(can_id)
