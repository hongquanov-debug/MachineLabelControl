"""
Main window, fixed 1024 x 768:

    header 52 px : brand | state pill | recipe | speed | safety chip | link LEDs | user | clock
    nav 92 px    : 6 page buttons (QButtonGroup)          | QStackedWidget (pages)
    footer 30 px : active alarms (red background when any)
"""
from __future__ import annotations

from datetime import datetime

import math

from PyQt6.QtCore import QPointF, QSize, Qt, QTimer
from PyQt6.QtGui import QIcon, QPainter, QPixmap, QColor, QPen
from PyQt6.QtWidgets import (QButtonGroup, QFrame, QGridLayout, QHBoxLayout, QLabel, QMainWindow,
                             QStackedWidget, QToolButton, QVBoxLayout, QWidget)

from ..controller import AppController
from .common import STATE_PILL, active_alarms, alarm_title, safety_problem
from .pages.camera import CameraPage
from .pages.config import ConfigPage
from .pages.dashboard import DashboardPage
from .pages.log import LogPage
from .pages.monitor import MonitorPage
from .pages.report import ReportPage
from .theme import C, repolish
from .widgets import Led, label, set_kind

NAV = [("dashboard", "Tổng quan"), ("camera", "Camera"), ("monitor", "Giám sát"),
       ("config", "Cấu hình"), ("report", "Báo cáo"), ("log", "Nhật ký")]


def nav_icon(key: str, color: str) -> QIcon:
    """Small line icons drawn with QPainter (no image files needed)."""
    pm = QPixmap(52, 52)
    pm.fill(Qt.GlobalColor.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    pen = QPen(QColor(color), 3.4)
    pen.setCapStyle(Qt.PenCapStyle.RoundCap)
    p.setPen(pen)
    if key == "dashboard":
        for r in ((6, 6, 16, 20), (28, 6, 18, 11), (28, 23, 18, 23), (6, 32, 16, 14)):
            p.drawRoundedRect(*r, 2, 2)
    elif key == "camera":
        p.drawRoundedRect(5, 14, 42, 30, 4, 4)
        p.drawEllipse(17, 20, 18, 18)
        p.drawLine(18, 14, 22, 8)
        p.drawLine(22, 8, 30, 8)
        p.drawLine(30, 8, 34, 14)
    elif key == "monitor":
        p.drawPolyline(*[QPointF(x, y) for x, y in ((4, 28), (14, 28), (20, 10), (30, 44), (36, 28), (48, 28))])
    elif key == "config":
        p.drawEllipse(18, 18, 16, 16)
        for i in range(8):
            a = i * math.pi / 4
            p.drawLine(int(26 + 13 * math.cos(a)), int(26 + 13 * math.sin(a)),
                       int(26 + 20 * math.cos(a)), int(26 + 20 * math.sin(a)))
    elif key == "report":
        p.drawLine(8, 44, 46, 44)
        p.drawLine(14, 44, 14, 26)
        p.drawLine(26, 44, 26, 10)
        p.drawLine(38, 44, 38, 30)
    else:
        p.drawRoundedRect(10, 5, 32, 42, 3, 3)
        for y in (17, 25, 33):
            p.drawLine(17, y, 35, y)
    p.end()
    return QIcon(pm)


class MainWindow(QMainWindow):
    def __init__(self, ctrl: AppController):
        super().__init__()
        self.ctrl = ctrl
        self.setWindowTitle("PMeMS VisionControl")
        root = QWidget()
        grid = QGridLayout(root)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(0)
        grid.addWidget(self._build_header(), 0, 0, 1, 2)
        grid.addWidget(self._build_nav(), 1, 0)
        main = QWidget()
        mv = QVBoxLayout(main)
        mv.setContentsMargins(0, 0, 0, 0)
        mv.setSpacing(0)
        self.bench = label("⚠ CHẾ ĐỘ BÀN THỬ – chuỗi an toàn bị BỎ QUA, KM1 bị KHÓA TẮT. Không dùng để vận hành máy.",
                           "benchBanner")
        self.bench.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.bench.hide()
        mv.addWidget(self.bench)
        self.stack = QStackedWidget()
        self.pages = [DashboardPage(ctrl), CameraPage(ctrl), MonitorPage(ctrl),
                      ConfigPage(ctrl), ReportPage(ctrl), LogPage(ctrl)]
        for pg in self.pages:
            self.stack.addWidget(pg)
        mv.addWidget(self.stack, 1)
        grid.addWidget(main, 1, 1)
        grid.addWidget(self._build_footer(), 2, 0, 1, 2)
        grid.setRowStretch(1, 1)
        grid.setColumnStretch(1, 1)
        self.setCentralWidget(root)

        ctrl.status_changed.connect(self._on_status)
        ctrl.plc_connection.connect(lambda ok, _m: self.led_plc.set("ok" if ok else "err"))
        ctrl.link_connection.connect(lambda ok, _m: self.led_485.set("ok" if ok else "err"))
        ctrl.vision_result.connect(self._on_vision)
        ctrl.job_done.connect(self._on_job)
        self._recipe_job = 0
        ctrl.recipe_changed.connect(self.refresh_recipe)
        clock = QTimer(self)
        clock.timeout.connect(self._tick_clock)
        clock.start(1000)
        self._tick_clock()
        QTimer.singleShot(500, self.refresh_recipe)

    # ---------------------------------------------------------------- build
    def _build_header(self) -> QFrame:
        f = QFrame()
        f.setObjectName("header")
        f.setFixedHeight(52)
        h = QHBoxLayout(f)
        h.setContentsMargins(10, 0, 10, 0)
        h.setSpacing(10)
        b = QVBoxLayout()
        b.setSpacing(0)
        b.addWidget(label("PMeMS", "brand"))
        b.addWidget(label("VisionControl", "brandSub"))
        h.addLayout(b)
        self.pill = label("–", "statePill")
        self.pill.setFixedHeight(34)
        self.pill.setMinimumWidth(116)
        self.pill.setAlignment(Qt.AlignmentFlag.AlignCenter)
        h.addWidget(self.pill)
        self.recipe = self._hdr_item(h, "RECIPE", "–")
        self.speed = self._hdr_item(h, "TỐC ĐỘ", "0 lon/ph")
        h.addStretch(1)
        self.chip = label("AN TOÀN OK", "safetyChip")
        self.chip.setFixedHeight(30)
        set_kind(self.chip, "ok")
        h.addWidget(self.chip)
        links = QGridLayout()
        links.setHorizontalSpacing(10)
        links.setVerticalSpacing(1)
        self.led_plc, self.led_485, self.led_cam1, self.led_cam2 = Led(9), Led(9), Led(9), Led(9)
        for i, (led, t) in enumerate(((self.led_plc, "PLC"), (self.led_cam1, "Cam1"),
                                      (self.led_485, "RS485"), (self.led_cam2, "Cam2"))):
            row = QHBoxLayout()
            row.setSpacing(4)
            row.addWidget(led)
            row.addWidget(label(t, "hdrSmall"))
            links.addLayout(row, i // 2, i % 2)
        h.addLayout(links)
        self.user = QToolButton()
        self.user.setText("👤 Vận hành")
        self.user.setStyleSheet(f"QToolButton{{background:{C['panel2']};border:1px solid {C['line']};"
                                "border-radius:6px;padding:6px 10px;}")
        h.addWidget(self.user)
        c = QVBoxLayout()
        c.setSpacing(0)
        self.clock = label("--:--:--", "clock")
        self.date = label("", "hdrSmall")
        self.clock.setAlignment(Qt.AlignmentFlag.AlignRight)
        self.date.setAlignment(Qt.AlignmentFlag.AlignRight)
        c.addWidget(self.clock)
        c.addWidget(self.date)
        h.addLayout(c)
        return f

    def _hdr_item(self, layout, title: str, value: str) -> QLabel:
        v = QVBoxLayout()
        v.setSpacing(0)
        v.addWidget(label(title, "hdrSmall"))
        val = label(value, "hdrValue")
        v.addWidget(val)
        layout.addLayout(v)
        return val

    def _build_nav(self) -> QFrame:
        f = QFrame()
        f.setObjectName("nav")
        f.setFixedWidth(92)
        v = QVBoxLayout(f)
        v.setContentsMargins(6, 6, 6, 6)
        v.setSpacing(4)
        self.nav = QButtonGroup(self)
        self.nav.setExclusive(True)
        self.nav_buttons = []
        for i, (key, text) in enumerate(NAV):
            b = QToolButton()
            b.setObjectName("navBtn")
            b.setText(text)
            b.setCheckable(True)
            b.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextUnderIcon)
            b.setIcon(nav_icon(key, C["muted"]))
            b.setIconSize(QSize(28, 28))
            b.setFixedSize(80, 80)
            self.nav.addButton(b, i)
            self.nav_buttons.append(b)
            v.addWidget(b)
        v.addStretch(1)
        self.nav_buttons[0].setChecked(True)
        self.nav.idClicked.connect(self.show_page)
        self.badge = QLabel(self.nav_buttons[-1])
        self.badge.setStyleSheet(f"background:{C['err']};color:white;border-radius:9px;font-size:11px;"
                                 "font-weight:700;padding:0 5px;")
        self.badge.move(50, 6)
        self.badge.hide()
        return f

    def _build_footer(self) -> QFrame:
        f = QFrame()
        f.setObjectName("footer")
        f.setFixedHeight(30)
        h = QHBoxLayout(f)
        h.setContentsMargins(12, 0, 12, 0)
        self.footer = f
        self.f_led = Led(10)
        self.f_led.set("ok")
        h.addWidget(self.f_led)
        self.f_text = label("Không có alarm", "footerText")
        h.addWidget(self.f_text, 1)
        mode = "Giả lập" if self.ctrl.sim else f"Modbus {self.ctrl.cfg.plc.host}:{self.ctrl.cfg.plc.port} · " \
                                               f"{self.ctrl.cfg.serial.port}"
        h.addWidget(label(mode, "hint"))
        return f

    # ---------------------------------------------------------------- behaviour
    def show_page(self, i: int) -> None:
        self.stack.setCurrentIndex(i)
        self.nav_buttons[i].setChecked(True)
        for j, b in enumerate(self.nav_buttons):
            b.setIcon(nav_icon(NAV[j][0], C["accent"] if j == i else C["muted"]))
        page = self.pages[i]
        if hasattr(page, "on_show"):
            page.on_show()

    def _tick_clock(self) -> None:
        now = datetime.now()
        self.clock.setText(now.strftime("%H:%M:%S"))
        self.date.setText(now.strftime("%d/%m/%Y"))

    def refresh_recipe(self) -> None:
        self._recipe_job = self.ctrl.active_recipe_id()

    def _on_job(self, job: int, result, err: str) -> None:
        """Header recipe: active_recipe_id() -> int, then read_recipe(id) -> Recipe."""
        if job != self._recipe_job or err:
            return
        if isinstance(result, int):
            self.ctrl.active_recipe = result
            self._recipe_job = self.ctrl.read_recipe(result)
        else:
            self.recipe.setText(f"{result.recipe_id} · {result.name}")
            self._recipe_job = 0

    def _on_status(self, st) -> None:
        text, kind = STATE_PILL.get(st.state_name, (st.state_name, ""))
        self.pill.setText(text)
        set_kind(self.pill, kind)
        self.speed.setText(f"{st.speed_cans_min} lon/ph")
        prob = safety_problem(st)
        if st.safety_bench_mode:
            self.chip.setText("BÀN THỬ")
            set_kind(self.chip, "bench")
        else:
            self.chip.setText(prob or "AN TOÀN OK")
            set_kind(self.chip, "err" if prob else "ok")
        self.bench.setVisible(st.safety_bench_mode)
        alarms = active_alarms(st)
        self.footer.setProperty("alarm", bool(alarms))
        repolish(self.footer)
        repolish(self.f_text)
        self.f_led.set("err" if alarms else "ok")
        self.f_text.setText("   •   ".join(alarm_title(a) for a in alarms) if alarms else "Không có alarm")
        self.badge.setText(str(len(alarms)))
        self.badge.adjustSize()
        self.badge.setVisible(bool(alarms))

    def _on_vision(self, rec) -> None:
        led = self.led_cam1 if rec.camera == 1 else self.led_cam2
        led.set("warn" if rec.late else "ok")

    def closeEvent(self, e) -> None:
        self.ctrl.shutdown()
        super().closeEvent(e)
