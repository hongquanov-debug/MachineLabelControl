"""Small helpers shared by the pages."""
from __future__ import annotations

from PyQt6.QtWidgets import QMessageBox, QWidget

from pmems_config.registers import ALARM_NAMES, MachineStatus

# firmware state name -> (text on the pill, colour kind)
STATE_PILL = {
    "POWER_ON": ("KHỞI ĐỘNG", ""), "ESTOP": ("E-STOP", "err"), "HOMING": ("HOMING", "warn"),
    "READY": ("READY", "ready"), "RUNNING": ("RUNNING", "run"), "ALARM": ("ALARM", "err"),
    "PAUSED": ("PAUSED", "warn"), "MANUAL_JOG": ("CHẠY TAY", "ready"), "HOMING_REQUIRED": ("CẦN HOMING", "warn"),
}

# Which machine commands are allowed in which state (same rules as firmware machine.c)
ENABLED = {
    "start": {"READY", "PAUSED"},
    "pause": {"RUNNING"},
    "stop": {"RUNNING", "PAUSED"},
    "reset": {"READY", "ALARM", "HOMING_REQUIRED", "ESTOP", "PAUSED", "RUNNING"},
    "home": {"HOMING_REQUIRED", "READY", "ALARM"},
}

ALARM_TEXT = {
    1: "Nút dừng khẩn cấp đang nhấn", 2: "Cửa bảo vệ bị mở", 3: "Áp suất khí nén < 0.5 MPa",
    4: "Nhiệt độ tủ điện > 55 °C", 5: "3 lon liên tiếp không có kết quả Vision", 6: "Đầu hút không giữ được tem",
    7: "Hết cuộn tem / đứt băng tem", 8: "Xi lanh dán P5 quá 150 ms",
    9: "Camera 2 không trả kết quả 3 lần liên tiếp", 10: "Lỗi driver M2", 11: "Lỗi servo M3",
    12: "Lỗi driver M4", 13: "Lỗi driver M5", 14: "Lỗi driver M6", 15: "Xi lanh không về đúng vị trí",
    16: "Về gốc thất bại", 17: "Biến tần báo lỗi", 18: "Tràn bộ đệm lon / kẹt lon", 19: "Mất kết nối biến tần",
    20: "Tuột cáp module ngõ vào LI", 21: "5 lon liên tiếp lỗi mã DataMatrix",
}
WARNING_ALARMS = {4, 6}


def alarm_title(code: int) -> str:
    return f"ALM_{code:02d} {ALARM_NAMES.get(code, '')}"


def active_alarms(st: MachineStatus) -> list[int]:
    return [c for c in range(1, 32) if st.alarm_mask >> c & 1]


def safety_problem(st: MachineStatus) -> str:
    """Text for the header chip, '' when everything is healthy (30024)."""
    if not st.io_bus_ok:
        return "MẤT CÁP LI"
    if not st.estop_ok:
        return "E-STOP"
    if not st.door_closed:
        return "CỬA MỞ"
    if not st.air_ok_sw:
        return "THIẾU KHÍ"
    return ""


def confirm(parent: QWidget, title: str, text: str) -> bool:
    box = QMessageBox(QMessageBox.Icon.Question, title, text,
                      QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel, parent)
    box.button(QMessageBox.StandardButton.Yes).setText("Đồng ý")
    box.button(QMessageBox.StandardButton.Cancel).setText("Hủy")
    return box.exec() == QMessageBox.StandardButton.Yes


def error(parent: QWidget, title: str, text: str) -> None:
    QMessageBox.warning(parent, title, text)
