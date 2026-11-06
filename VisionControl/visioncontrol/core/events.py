"""
Turns consecutive MachineStatus readings into log entries (state changes,
alarms raised / cleared, safety, counters wrap). Pure logic, unit tested.
"""
from __future__ import annotations

from typing import Optional

from pmems_config.registers import ALARM_NAMES, STATE_NAMES, MachineStatus

from .models import CAT_ALARM, CAT_EVENT, LEVEL_ERR, LEVEL_INFO, LEVEL_WARN, LogEntry

# alarms that are warnings only (firmware severity WARNING)
WARNING_ALARMS = {4, 6}

STATE_TEXT = {
    "POWER_ON": "Khởi động", "ESTOP": "Dừng khẩn cấp", "HOMING": "Đang về gốc", "READY": "Sẵn sàng",
    "RUNNING": "Đang chạy", "ALARM": "Dừng do lỗi", "PAUSED": "Tạm dừng", "MANUAL_JOG": "Chạy tay",
    "HOMING_REQUIRED": "Cần về gốc",
}


def state_text(state: int) -> str:
    name = STATE_NAMES.get(state, f"STATE_{state}")
    return STATE_TEXT.get(name, name)


class EventDetector:
    def __init__(self) -> None:
        self.prev: Optional[MachineStatus] = None

    def update(self, st: MachineStatus) -> list[LogEntry]:
        out: list[LogEntry] = []
        p = self.prev
        self.prev = st
        if p is None:
            out.append(LogEntry(LEVEL_INFO, CAT_EVENT, "FSM", f"Trạng thái máy: {state_text(st.state)}"))
            if st.safety_bench_mode:
                out.append(LogEntry(LEVEL_WARN, CAT_EVENT, "Safety",
                                    "Firmware bản BÀN THỬ: chuỗi an toàn bị bỏ qua, KM1 bị khóa"))
            p_mask = 0
        else:
            p_mask = p.alarm_mask
            if st.state != p.state:
                out.append(LogEntry(LEVEL_INFO, CAT_EVENT, "FSM",
                                    f"Trạng thái: {state_text(p.state)} → {state_text(st.state)}"))
        raised = st.alarm_mask & ~p_mask
        cleared = p_mask & ~st.alarm_mask
        for code in range(1, 32):
            name = ALARM_NAMES.get(code, f"ALM_{code:02d}")
            if raised >> code & 1:
                lvl = LEVEL_WARN if code in WARNING_ALARMS else LEVEL_ERR
                out.append(LogEntry(lvl, CAT_ALARM, "Alarm", name, code=f"ALM_{code:02d}"))
            if cleared >> code & 1:
                out.append(LogEntry(LEVEL_INFO, CAT_ALARM, "Alarm", f"Đã xóa: {name}", code=f"ALM_{code:02d}"))
        return out
