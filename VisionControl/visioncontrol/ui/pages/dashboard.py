"""
Tổng quan: machine commands, production KPIs, conveyor, camera 1 preview,
last cans, active alarms.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtWidgets import (QGridLayout, QHBoxLayout, QHeaderView, QListWidget, QListWidgetItem,
                             QProgressBar, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget)

from pmems_config.registers import MachineCmd

from ...controller import AppController
from ..common import ALARM_TEXT, ENABLED, WARNING_ALARMS, active_alarms, alarm_title, confirm
from ..theme import C
from ..widgets import Card, ConveyorView, ImageView, KpiCard, button, label


class DashboardPage(QWidget):
    def __init__(self, ctrl: AppController):
        super().__init__()
        self.ctrl = ctrl
        g = QGridLayout(self)
        g.setContentsMargins(10, 10, 10, 10)
        g.setSpacing(10)

        # ---- control card (left, 2 rows)
        ctl = Card("Điều khiển máy")
        bg = QGridLayout()
        bg.setSpacing(8)
        self.btn = {
            "start": button("▶  START", "ok", big=True), "pause": button("❚❚  PAUSE", "warn", big=True),
            "stop": button("■  STOP", "err", big=True), "reset": button("↺  RESET", big=True),
            "home": button("⌂  HOMING – về gốc && xả băng tải", "primary", big=True),
        }
        bg.addWidget(self.btn["start"], 0, 0)
        bg.addWidget(self.btn["pause"], 0, 1)
        bg.addWidget(self.btn["stop"], 1, 0)
        bg.addWidget(self.btn["reset"], 1, 1)
        bg.addWidget(self.btn["home"], 2, 0, 1, 2)
        ctl.body.addLayout(bg)
        self.btn["start"].clicked.connect(lambda: ctrl.command(MachineCmd.START))
        self.btn["pause"].clicked.connect(lambda: ctrl.command(MachineCmd.PAUSE))
        self.btn["stop"].clicked.connect(self._stop)
        self.btn["reset"].clicked.connect(self._reset)
        self.btn["home"].clicked.connect(self._home)
        self.speed_set = 120
        self.offset = 0.0
        self.speed_lbl = self._stepper(ctl, "Tốc độ đặt", "lon/phút (60–156)", "120",
                                       lambda d: self._change_speed(5 * d))
        self.offset_lbl = self._stepper(ctl, "Offset góc", "độ (−18 … +18)", "0.0",
                                        lambda d: self._change_offset(0.5 * d))
        ctl.body.addStretch(1)
        g.addWidget(ctl, 0, 0, 2, 1)

        # ---- KPIs
        kp = QGridLayout()
        kp.setSpacing(10)
        self.k_speed = KpiCard("Tốc độ thực", "lon/phút")
        self.k_fed = KpiCard("Tổng lon", "đã nạp")
        self.k_ok = KpiCard("Đạt (OK)", "dán tem đạt", "ok")
        self.k_ng = KpiCard("Lỗi (NG)", "bị loại P8", "err")
        self.k_yield = KpiCard("Tỉ lệ đạt", "")
        self.yield_bar = QProgressBar()
        self.yield_bar.setTextVisible(False)
        self.k_yield.layout().addWidget(self.yield_bar)
        self.k_belt = KpiCard("Trên băng tải", "lon đang theo dõi")
        self.k_code = KpiCard("Mã DM đạt", "camera 2")
        for i, w in enumerate((self.k_speed, self.k_fed, self.k_ok, self.k_ng)):
            kp.addWidget(w, 0, i)
        kp.addWidget(self.k_yield, 1, 0, 1, 2)
        kp.addWidget(self.k_belt, 1, 2)
        kp.addWidget(self.k_code, 1, 3)
        kw = QWidget()
        kw.setLayout(kp)
        kp.setContentsMargins(0, 0, 0, 0)
        kw.setFixedHeight(160)
        g.addWidget(kw, 0, 1)

        # ---- conveyor
        conv = Card("Băng tải", label("● chờ  ● có góc  ● đã dán / đạt  ● lỗi", "hint"))
        self.conveyor = ConveyorView()
        conv.body.addWidget(self.conveyor, 1)
        g.addWidget(conv, 1, 1)

        # ---- bottom row
        bottom = QHBoxLayout()
        bottom.setSpacing(10)
        cam = Card("Camera 1")
        self.cam_info = label("", "hint")
        self.mini = ImageView()
        cam.body.addWidget(self.mini, 1)
        cam.setFixedWidth(290)
        bottom.addWidget(cam)
        rc = Card("Lon gần nhất")
        self.recent = QTableWidget(0, 5)
        self.recent.setHorizontalHeaderLabels(["ID", "Góc θ", "Cam1", "Cam2", "KQ"])
        self.recent.verticalHeader().hide()
        self.recent.setWordWrap(False)
        self.recent.verticalHeader().setDefaultSectionSize(24)
        self.recent.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.recent.horizontalHeader().setMinimumSectionSize(40)
        self.recent.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.recent.setSelectionMode(QTableWidget.SelectionMode.NoSelection)
        rc.body.addWidget(self.recent)
        bottom.addWidget(rc, 1)
        al = Card("Alarm đang hoạt động")
        self.alarms = QListWidget()
        self.alarms.setWordWrap(True)
        self._shown_alarms: list | None = None
        al.body.addWidget(self.alarms)
        al.setFixedWidth(280)
        bottom.addWidget(al)
        g.addLayout(bottom, 2, 0, 1, 2)
        g.setColumnMinimumWidth(0, 290)
        g.setColumnStretch(1, 1)
        g.setRowMinimumHeight(0, 160)
        g.setRowMinimumHeight(1, 190)
        g.setRowStretch(2, 1)

        ctrl.status_changed.connect(self._on_status)
        ctrl.vision_result.connect(self._on_vision)
        t = QTimer(self)
        t.timeout.connect(self._refresh_conveyor)
        t.start(100)
        self._update_buttons("POWER_ON")

    def _stepper(self, card: Card, title: str, sub: str, value: str, on_step) -> object:
        row = QHBoxLayout()
        tv = QVBoxLayout()
        tv.setSpacing(0)
        tv.addWidget(label(title))
        tv.addWidget(label(sub, "hint"))
        row.addLayout(tv, 1)
        minus, plus = button("−", size="sm"), button("+", size="sm")
        val = label(value, "statValue")
        val.setMinimumWidth(52)
        val.setAlignment(Qt.AlignmentFlag.AlignCenter)
        minus.clicked.connect(lambda: on_step(-1))
        plus.clicked.connect(lambda: on_step(+1))
        row.addWidget(minus)
        row.addWidget(val)
        row.addWidget(plus)
        card.body.addLayout(row)
        return val

    # ---------------------------------------------------------------- actions
    def _change_speed(self, d: int) -> None:
        self.speed_set = max(60, min(156, self.speed_set + d))
        self.speed_lbl.setText(str(self.speed_set))
        self.ctrl.set_speed(self.speed_set)

    def _change_offset(self, d: float) -> None:
        self.offset = max(-18.0, min(18.0, self.offset + d))
        self.offset_lbl.setText(f"{self.offset:+.1f}")
        self.ctrl.set_theta_offset(self.offset)

    def _stop(self) -> None:
        if confirm(self, "Dừng máy?", "Ngừng cấp lon và dừng băng tải. Các lon trên băng tải vẫn được theo dõi."):
            self.ctrl.command(MachineCmd.STOP)

    def _reset(self) -> None:
        st = self.ctrl.status
        if st is not None and st.state_name == "ESTOP":
            confirm(self, "Đang E-STOP", "Nút RESET trên màn hình không đưa máy ra khỏi E-Stop.\n\n"
                    "Nhả nút E-Stop → đóng cửa bảo vệ → nhấn nút SAFETY_RESET trên tủ điện.")
            return
        self.ctrl.command(MachineCmd.RESET_ALARM)

    def _home(self) -> None:
        if confirm(self, "Về gốc toàn máy?", "Nhả P4, nâng P5, xả băng tải 5 giây rồi về gốc các trục "
                   "M6, M3, M4, M5.\nĐảm bảo không có người trong vùng máy."):
            self.ctrl.command(MachineCmd.START_HOMING)

    # ---------------------------------------------------------------- updates
    def _update_buttons(self, state: str) -> None:
        for k, b in self.btn.items():
            b.setEnabled(state in ENABLED[k])

    def _on_status(self, st) -> None:
        self._update_buttons(st.state_name)
        total = st.ok + st.ng
        self.k_speed.set(str(st.speed_cans_min))
        self.k_fed.set(f"{st.fed:,}".replace(",", "."))
        self.k_ok.set(f"{st.ok:,}".replace(",", "."))
        self.k_ng.set(f"{st.ng:,}".replace(",", "."))
        y = st.ok / total * 100 if total else 0.0
        self.k_yield.set(f"{y:.1f} %" if total else "–")
        self.yield_bar.setValue(int(y))
        self.k_belt.set(str(st.cans_on_belt))
        ct = st.code_ok + st.code_ng
        self.k_code.set(f"{st.code_ok / ct * 100:.1f} %" if ct else "–", f"OK {st.code_ok} · NG {st.code_ng}")
        codes = active_alarms(st)
        if codes != self._shown_alarms:
            self._shown_alarms = codes
            self.alarms.clear()
            if not codes:
                it = QListWidgetItem("Không có alarm")
                it.setForeground(Qt.GlobalColor.gray)
                self.alarms.addItem(it)
            for c in codes:
                it = QListWidgetItem(f"{alarm_title(c)}\n{ALARM_TEXT.get(c, '')}")
                it.setData(Qt.ItemDataRole.UserRole, c)
                it.setForeground(Qt.GlobalColor.yellow if c in WARNING_ALARMS else Qt.GlobalColor.red)
                self.alarms.addItem(it)

    def _on_vision(self, rec) -> None:
        if rec.camera == 1:
            self.mini.set_frame(rec.image, rec.overlay,
                                f"#{rec.can_id}  " + (f"{rec.theta_deg:+.2f}°" if rec.ok else rec.quality_name),
                                f"{rec.elapsed_ms:.0f} ms")
        rows = list(self.ctrl.recent)[:8]
        self.recent.setRowCount(len(rows))
        for i, r in enumerate(rows):
            cells = [f"#{r['id']}", "–" if r["theta"] is None else f"{r['theta']:.2f}°",
                     r["cam1"] or "–", r["cam2"] or "–"]
            bad = (r["cam1"] not in (None, "OK")) or (r["cam2"] not in (None, "OK"))
            res = "NG" if bad else ("Đạt" if r["cam2"] == "OK" else "Chờ")
            for j, t in enumerate(cells + [res]):
                it = QTableWidgetItem(t)
                if j >= 2 and t not in ("–", "Chờ"):
                    it.setForeground(Qt.GlobalColor.green if t in ("OK", "Đạt") else Qt.GlobalColor.red)
                self.recent.setItem(i, j, it)

    def _refresh_conveyor(self) -> None:
        self.conveyor.set_cans(self.ctrl.tracker.snapshot(), self.ctrl.belt_mm)
