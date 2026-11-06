"""
Giám sát: stations & axes, 32 DI / 24 DO, manual jog + outputs (MANUAL_JOG only),
safety chain (30024) with the PCB v1 wiring check.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (QGridLayout, QHBoxLayout, QHeaderView, QTableWidget, QTableWidgetItem,
                             QTabWidget, QVBoxLayout, QWidget, QFrame, QPushButton)

from pmems_config.registers import MANUAL_OUTPUTS, MachineCmd

from ...controller import AppController
from ..common import confirm, error
from ..theme import C
from ..widgets import Card, Led, LedLabel, Stat, button, label, set_css, set_kind

DI_NAMES = ["S7_LANE1", "S8_LANE2", "S9_LANE3", "S10_LANE4", "P1_EXT", "P1_RET", "P2_EXT", "P2_RET",
            "P3_EXT", "P3_RET", "P4_EXT", "P4_RET", "P5_UP", "P5_DOWN", "P8_EXT", "P8_RET",
            "VAC_SW_1", "VAC_SW_2", "VAC_SW_3", "VAC_SW_4", "VAC_SW_5", "VAC_SW_6", "S0_QUEUE", "S1_RELEASE",
            "ALM_M2", "ALM_M3", "ALM_M4", "ALM_M5", "ALM_M6A", "ALM_M6B", "S14_TAPE_END", "SPARE_32"]
DO_NAMES = ["CYL_P1", "CYL_P2", "CYL_P3", "CYL_P4", "CYL_P5", "VALVE_P7", "CYL_P8", "MTR_M7",
            "VAC_HEAD_1", "VAC_HEAD_2", "VAC_HEAD_3", "VAC_HEAD_4", "VAC_HEAD_5", "VAC_HEAD_6", "VAC_BLOW", "SPARE_16",
            "ENA_M2", "ENA_M4", "ENA_M5", "ENA_M6", "SON_M3", "TOWER_RED", "TOWER_YEL", "TOWER_GRN"]
ALARM_DI = range(24, 31)          # driver alarms + tape end: LED red when ON

# manual output name -> (description, DI bit "out/down", DI bit "in/up") ; None = no sensor
CYLINDERS = [("P1", "Chặn lon A", 4, 5), ("P2", "Nêm chen khe", 6, 7), ("P3", "Chốt chỉnh góc", 8, 9),
             ("P4", "Kẹp lon", 10, 11), ("P5", "Hạ dán tem", 13, 12), ("P8", "Đẩy loại", 14, 15),
             ("P7", "Thổi hỗ trợ", None, None), ("M7", "Motor cuộn đế", None, None),
             ("VAC_BLOW", "Thổi nhả tem", None, None)]
AXES = [("M2", "Chỉnh góc (bánh PU)", 24), ("M3", "Mâm xoay 6 đầu", 25), ("M4", "Kéo tem", 26),
        ("M5", "Dịch làn tem", 27), ("M6", "Nâng hạ (M6a+b)", 28)]

SAFETY = [("estop", "Nút dừng khẩn cấp", "Đã nhả", "ĐANG NHẤN", 0),
          ("door", "Cửa bảo vệ", "Đóng", "ĐANG MỞ", 1),
          ("air", "Áp suất khí nén", "≥ 0.5 MPa", "THIẾU KHÍ", 2),
          ("reset", "Nút SAFETY_RESET", "Không nhấn", "ĐANG NHẤN", 3),
          ("bus", "Cáp module LI", "Kết nối tốt", "MẤT KẾT NỐI", -1)]
WIRING_CHECK = [("Bình thường (nhả E-Stop, đóng cửa, đủ khí)", "0x07"), ("Nhấn E-Stop", "0x06"),
                ("Mở cửa bảo vệ", "0x05"), ("Khóa van khí", "0x03"), ("Giữ nút SAFETY_RESET", "0x0F"),
                ("Rút cáp LI", "Bus --")]


class Tile(QFrame):
    """Big safety / power tile: title, LED + state text, source."""

    def __init__(self, title: str, source: str = ""):
        super().__init__()
        self.setObjectName("card")
        v = QVBoxLayout(self)
        v.setContentsMargins(12, 8, 12, 8)
        v.setSpacing(2)
        v.addWidget(label(title, "hint"))
        row = QHBoxLayout()
        self.led = Led(14)
        self.text = label("–")
        self.text.setStyleSheet("font-size:16px; font-weight:700;")
        row.addWidget(self.led)
        row.addWidget(self.text, 1)
        v.addLayout(row)
        self.src = label(source, "hint")
        v.addWidget(self.src)

    def set(self, text: str, color: str, bad: bool = False) -> None:
        self.text.setText(text)
        self.led.set(color)
        set_css(self, f"QFrame#card{{border:1px solid {C['err'] if bad else C['line']};"
                           f"background:{'rgba(239,68,68,0.12)' if bad else C['panel']};}}")


class MonitorPage(QWidget):
    def __init__(self, ctrl: AppController):
        super().__init__()
        self.ctrl = ctrl
        v = QVBoxLayout(self)
        v.setContentsMargins(10, 4, 10, 10)
        self.tabs = QTabWidget()
        self.tabs.addTab(self._tab_stations(), "Trạm && trục")
        self.tabs.addTab(self._tab_io(), "Ngõ vào / ra")
        self.tabs.addTab(self._tab_jog(), "Chạy tay")
        self.tabs.addTab(self._tab_safety(), "An toàn")
        v.addWidget(self.tabs)
        ctrl.status_changed.connect(self._on_status)
        ctrl.job_done.connect(self._on_job)
        self._jobs: set[int] = set()

    # ================================================================ tabs
    def _tab_stations(self) -> QWidget:
        w = QWidget()
        g = QGridLayout(w)
        g.setContentsMargins(0, 8, 0, 0)
        g.setSpacing(10)
        self.st = {}
        for i, (key, title) in enumerate((("feed", "Cấp lon P1/P2"), ("orient", "Chỉnh góc P3/P4"),
                                          ("label", "Dán tem P5"), ("cam2", "Đọc mã DM"), ("rej", "Loại P8"))):
            c = Card(title)
            state = label("–")
            state.setStyleSheet("font-size:14px; font-weight:700;")
            info = label("", "muted", wrap=True)
            c.body.addWidget(state)
            c.body.addWidget(info)
            c.body.addStretch(1)
            self.st[key] = (state, info)
            g.addWidget(c, 0, i)
        axes = Card("Trục động cơ")
        self.axis = QTableWidget(len(AXES), 4)
        self.axis.setHorizontalHeaderLabels(["Trục", "Chức năng", "Driver", "Vị trí"])
        self.axis.verticalHeader().hide()
        self.axis.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.axis.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        for r, (n, f, _) in enumerate(AXES):
            self.axis.setItem(r, 0, QTableWidgetItem(n))
            self.axis.setItem(r, 1, QTableWidgetItem(f))
        axes.body.addWidget(self.axis)
        axes.body.addWidget(label("Firmware chỉ công bố vị trí M6 (30010). Vị trí các trục khác cần "
                                  "thêm thanh ghi nếu muốn hiển thị.", "hint", wrap=True))
        g.addWidget(axes, 1, 0, 1, 3)
        side = Card("An toàn & biến tần")
        self.sum = {k: LedLabel(t) for k, t in (("estop", "E-Stop nhả"), ("door", "Cửa đóng"), ("air", "Đủ khí nén"),
                                                ("bus", "Cáp LI"), ("km1", "KM1 đóng"), ("bench", "Bàn thử"))}
        sg = QGridLayout()
        sg.setSpacing(6)
        for i, wdg in enumerate(self.sum.values()):
            sg.addWidget(wdg, i // 2, i % 2)
        side.body.addLayout(sg)
        vg = QGridLayout()
        vg.setSpacing(6)
        self.vfd = {k: Stat(t) for k, t in (("hz", "Tần số ra"), ("a", "Dòng điện"))}
        vg.addWidget(self.vfd["hz"], 0, 0)
        vg.addWidget(self.vfd["a"], 0, 1)
        side.body.addLayout(vg)
        side.body.addStretch(1)
        g.addWidget(side, 1, 3, 1, 2)
        g.setRowStretch(1, 1)
        return w

    def _tab_io(self) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 8, 0, 0)
        h.setSpacing(10)
        self.di, self.do = [], []
        for title, names, store in (("32 ngõ vào – LI32 (30012–13)", DI_NAMES, self.di),
                                    ("24 ngõ ra – LO24 (30014–15)", DO_NAMES, self.do)):
            c = Card(title)
            g = QGridLayout()
            g.setSpacing(4)
            for i, n in enumerate(names):
                ll = LedLabel(n, f"{i // 8}.{i % 8}")
                store.append(ll)
                g.addWidget(ll, i // 2, i % 2)
            c.body.addLayout(g)
            c.body.addStretch(1)
            h.addWidget(c)
        return w

    def _tab_jog(self) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 8, 0, 0)
        h.setSpacing(10)
        left = Card("Chạy tay trục")
        row = QHBoxLayout()
        row.setSpacing(6)
        self.axis_btn = []
        for i, (n, _, _) in enumerate(AXES):
            b = button(n)
            b.setCheckable(True)
            b.setMinimumWidth(0)
            b.clicked.connect(lambda _=False, k=i: self._pick_axis(k))
            self.axis_btn.append(b)
            row.addWidget(b)
        left.body.addLayout(row)
        self.jog_axis = 0
        self.axis_btn[0].setChecked(True)
        pad = QHBoxLayout()
        self.jog_neg, self.jog_pos = button("◀  −"), button("+  ▶")
        for b, d in ((self.jog_neg, -1), (self.jog_pos, 1)):
            b.setMinimumHeight(90)
            b.setStyleSheet("font-size:20px;")
            b.pressed.connect(lambda d=d: self.ctrl.jog(self.jog_axis, d))
            b.released.connect(lambda: self.ctrl.jog(self.jog_axis, 0))
            pad.addWidget(b)
        left.body.addLayout(pad)
        left.body.addWidget(label("Nhấn giữ để chạy (40011 = ±1), thả ra để dừng (40011 = 0). M3 chỉ chạy khi "
                                  "P5 đã lên. Chỉ hoạt động ở chế độ MANUAL_JOG.", "hint", wrap=True))
        left.body.addStretch(1)
        self.jog_mode = button("Vào chế độ chạy tay", "primary")
        self.jog_mode.clicked.connect(self._toggle_jog)
        left.body.addWidget(self.jog_mode)
        left.setFixedWidth(320)
        h.addWidget(left)
        right = Card("Xi lanh & van (40012)", label("nhấn để đảo trạng thái", "hint"))
        g = QGridLayout()
        g.setSpacing(8)
        self.cyl = {}
        for i, (name, desc, s_out, s_in) in enumerate(CYLINDERS):
            box = QFrame()
            box.setObjectName("stat")
            bv = QVBoxLayout(box)
            bv.setContentsMargins(8, 6, 8, 6)
            top = QHBoxLayout()
            top.addWidget(label(name))
            top.addStretch(1)
            top.addWidget(label(desc, "hint"))
            bv.addLayout(top)
            leds = QHBoxLayout()
            l_out, l_in = Led(), Led()
            if s_out is not None:
                leds.addWidget(l_out)
                leds.addWidget(label("Xuống" if name == "P5" else "Ra", "hint"))
                leds.addStretch(1)
                leds.addWidget(l_in)
                leds.addWidget(label("Lên" if name == "P5" else "Về", "hint"))
            else:
                leds.addWidget(l_out)
                leds.addWidget(label("Đang bật", "hint"))
                leds.addStretch(1)
            bv.addLayout(leds)
            b = button("Đảo", size="sm")
            b.clicked.connect(lambda _=False, n=name: self._toggle_output(n))
            bv.addWidget(b)
            self.cyl[name] = (l_out, l_in, b, s_out, s_in)
            g.addWidget(box, i // 3, i % 3)
        right.body.addLayout(g)
        right.body.addStretch(1)
        h.addWidget(right, 1)
        return w

    def _tab_safety(self) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 8, 0, 0)
        h.setSpacing(10)
        left = Card("Chuỗi an toàn",
                    label("nhấn ô để giả lập" if self.ctrl.sim else "chỉ hiển thị", "hint"))
        g = QGridLayout()
        g.setSpacing(8)
        self.tiles = {}
        for i, (k, title, _ok, _bad, bit) in enumerate(SAFETY):
            t = Tile(title, "30024 bit4" if bit < 0 else f"LI mở rộng · bit {bit}")
            if self.ctrl.sim:
                t.mousePressEvent = lambda _e, key=k: self._sim_toggle(key)
                t.setCursor(Qt.CursorShape.PointingHandCursor)
            self.tiles[k] = t
            g.addWidget(t, i // 3, i % 3)
        left.body.addLayout(g)
        left.body.addWidget(label("NGUỒN ĐỘNG LỰC", "cardTitle"))
        pw = QHBoxLayout()
        self.t_km1, self.t_brake = Tile("Contactor KM1", "GPIO PC4"), Tile("Phanh trục nâng M6", "GPIO PC5")
        pw.addWidget(self.t_km1)
        pw.addWidget(self.t_brake)
        left.body.addLayout(pw)
        self.help = label("<b>Máy đang ở E-STOP.</b> Để thoát: nhả nút E-Stop → đóng cửa bảo vệ → nhấn nút "
                          "<b>SAFETY_RESET</b> trên tủ điện → máy chuyển sang “Cần homing”. Nút RESET trên màn "
                          "hình chỉ xóa alarm, không đưa máy ra khỏi E-Stop.", "helpBox", wrap=True)
        self.help.hide()
        left.body.addWidget(self.help)
        left.body.addStretch(1)
        h.addWidget(left, 1)
        right = Card("Mạch LI mở rộng", label("PCB v1 · 74HCT165", "hint"))
        rr = QHBoxLayout()
        rr.addWidget(label("Giá trị thô (byte cao 30024)", "muted"))
        rr.addStretch(1)
        self.raw = label("0x00", "bigValue")
        self.raw.setStyleSheet("font-size:24px;")
        rr.addWidget(self.raw)
        right.body.addLayout(rr)
        bits = QHBoxLayout()
        bits.setSpacing(4)
        self.bits = []
        for i in range(7, -1, -1):
            b = QFrame()
            b.setObjectName("stat")
            bv = QVBoxLayout(b)
            bv.setContentsMargins(2, 3, 2, 3)
            bv.setSpacing(0)
            val = label("0", "statValue")
            val.setAlignment(Qt.AlignmentFlag.AlignCenter)
            name = {0: "E-Stop", 1: "Cửa", 2: "Khí", 3: "Reset"}.get(i, "–")
            cap = label(f"b{i}\n{name}", "hint")
            cap.setAlignment(Qt.AlignmentFlag.AlignCenter)
            bv.addWidget(val)
            bv.addWidget(cap)
            self.bits.insert(0, val)
            bits.addWidget(b)
        right.body.addLayout(bits)
        right.body.addWidget(label("KIỂM TRA ĐẤU DÂY", "cardTitle"))
        self.check = QTableWidget(len(WIRING_CHECK), 3)
        self.check.setHorizontalHeaderLabels(["Thao tác", "Phải đọc ra", ""])
        self.check.verticalHeader().hide()
        self.check.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.check.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        for r, (t, v) in enumerate(WIRING_CHECK):
            self.check.setItem(r, 0, QTableWidgetItem(t))
            self.check.setItem(r, 1, QTableWidgetItem(v))
        right.body.addWidget(self.check)
        h.addWidget(right, 1)
        return w

    # ================================================================ actions
    def _pick_axis(self, i: int) -> None:
        self.jog_axis = i
        for j, b in enumerate(self.axis_btn):
            b.setChecked(j == i)

    def _toggle_jog(self) -> None:
        st = self.ctrl.status
        if st and st.state_name == "MANUAL_JOG":
            self.ctrl.command(MachineCmd.JOG_EXIT)
        elif st and st.state_name == "READY":
            self.ctrl.command(MachineCmd.JOG_ENTER)
        else:
            error(self, "Chạy tay", "Chỉ vào chế độ chạy tay khi máy ở READY.")

    def _toggle_output(self, name: str) -> None:
        st = self.ctrl.status
        if not st or st.state_name != "MANUAL_JOG":
            error(self, "Chạy tay", "Chỉ điều khiển xi lanh / van ở chế độ chạy tay (MANUAL_JOG).")
            return
        on = not bool(st.do >> MANUAL_OUTPUTS[name] & 1)
        if name == "P5" and on and not confirm(self, "Hạ xi lanh dán P5?", "Đảm bảo mâm M3 đứng yên và không "
                                                                         "có tay trong vùng dán."):
            return
        self._jobs.add(self.ctrl.set_output(name, on))

    def _on_job(self, job: int, _res, err: str) -> None:
        if job in self._jobs:
            self._jobs.discard(job)
            if err:
                error(self, "Bị từ chối", err)

    def _sim_toggle(self, key: str) -> None:
        st = self.ctrl.status
        if st is None:
            return
        if key == "reset":
            self.ctrl.sim_set_safety("reset", True)
            from PyQt6.QtCore import QTimer
            QTimer.singleShot(1000, lambda: self.ctrl.sim_set_safety("reset", False))
            return
        cur = {"estop": st.estop_ok, "door": st.door_closed, "air": st.air_ok_sw, "bus": st.io_bus_ok}[key]
        if key != "bus" and not st.io_bus_ok:
            cur = self.ctrl.mcu.safety[key]
        self.ctrl.sim_set_safety(key, not cur)

    # ================================================================ status
    def _on_status(self, st) -> None:
        di, do = st.di, st.do
        for i, w in enumerate(self.di):
            on = bool(di >> i & 1)
            w.set(("err" if i in ALARM_DI else "ok") if on else "off")
        for i, w in enumerate(self.do):
            w.set("ok" if do >> i & 1 else "off")
        running = st.state_name == "RUNNING"
        set_ = lambda k, t, col: (self.st[k][0].setText(t), set_css(self.st[k][0], 
            f"font-size:14px;font-weight:700;color:{C[col]};"))
        set_("feed", "Đang cấp" if running else "Chặn (P1 ra)", "ok" if running else "muted")
        self.st["feed"][1].setText(f"Hàng chờ S0: {'có lon' if di >> 22 & 1 else 'trống'}")
        set_("orient", "P3 ra" if di >> 8 & 1 else "Chờ lon", "info" if di >> 8 & 1 else "muted")
        self.st["orient"][1].setText(f"P4: {'kẹp' if di >> 10 & 1 else 'nhả'}")
        set_("label", "P5 xuống" if di >> 13 & 1 else "P5 lên", "ok" if running else "muted")
        vac = sum(di >> (16 + i) & 1 for i in range(6))
        self.st["label"][1].setText(f"Chân không đạt: {vac}/6")
        cq = st.code_last_quality
        set_("cam2", "Đọc mã" if running else "Chờ lon", "info" if running else "muted")
        self.st["cam2"][1].setText(f"Đạt {st.code_ok} · Lỗi {st.code_ng}\nGần nhất: {'OK' if cq == 0 else hex(cq)}")
        set_("rej", "Đang đẩy" if di >> 14 & 1 else "Sẵn sàng", "err" if di >> 14 & 1 else "muted")
        self.st["rej"][1].setText(f"Đã loại: {st.ng}")
        for r, (_n, _f, alm_bit) in enumerate(AXES):
            fault = bool(di >> alm_bit & 1) or (r == 4 and di >> 29 & 1)
            self.axis.setItem(r, 2, QTableWidgetItem("LỖI" if fault else "OK"))
            self.axis.item(r, 2).setForeground(Qt.GlobalColor.red if fault else Qt.GlobalColor.green)
            self.axis.setItem(r, 3, QTableWidgetItem(f"{st.m6_height_mm:.1f} mm" if r == 4 else "—"))
        km1 = st.state_name not in ("ESTOP", "POWER_ON") and not st.safety_bench_mode
        for k, ok in (("estop", st.estop_ok), ("door", st.door_closed), ("air", st.air_ok_sw),
                      ("bus", st.io_bus_ok), ("km1", km1)):
            self.sum[k].set("ok" if ok else "err", bad=not ok)
        self.sum["bench"].set("warn" if st.safety_bench_mode else "off")
        self.vfd["hz"].set(f"{st.vfd_freq_hz:.2f} Hz")
        self.vfd["a"].set(f"{st.vfd_current_a:.1f} A")
        # jog tab
        jog = st.state_name == "MANUAL_JOG"
        self.jog_mode.setText("Thoát chế độ chạy tay" if jog else "Vào chế độ chạy tay")
        for b in (self.jog_neg, self.jog_pos):
            b.setEnabled(jog)
        for name, (l_out, l_in, b, s_out, s_in) in self.cyl.items():
            b.setEnabled(jog)
            if s_out is None:
                l_out.set("ok" if do >> MANUAL_OUTPUTS[name] & 1 else "off")
            else:
                l_out.set("ok" if di >> s_out & 1 else "off")
                l_in.set("ok" if di >> s_in & 1 else "off")
        # safety tab
        vals = {"estop": st.estop_ok, "door": st.door_closed, "air": st.air_ok_sw, "reset": st.reset_pressed,
                "bus": st.io_bus_ok}
        for k, _title, ok_t, bad_t, _bit in SAFETY:
            t = self.tiles[k]
            if st.safety_bench_mode and k != "bus":
                t.set("BỎ QUA", "off")
            elif k == "reset":
                t.set(bad_t if vals[k] else ok_t, "info" if vals[k] else "ok")
            else:
                t.set(ok_t if vals[k] else bad_t, "ok" if vals[k] else "err", bad=not vals[k])
        self.t_km1.set("KHÓA TẮT" if st.safety_bench_mode else ("Đóng" if km1 else "Cắt"),
                       "ok" if km1 else "err", bad=not km1)
        self.t_brake.set("Nhả" if km1 else "Đóng phanh", "ok" if km1 else "info")
        self.help.setVisible(st.state_name == "ESTOP")
        raw = st.safety_ext_raw
        self.raw.setText(f"0x{raw:02X}")
        for i, lb in enumerate(self.bits):
            one = raw >> i & 1
            lb.setText(str(one))
            set_css(lb, f"color:{C['ok'] if one else C['muted']};")
        current = [vals["estop"] and vals["door"] and vals["air"] and not vals["reset"] and vals["bus"],
                   not vals["estop"] and vals["bus"], not vals["door"] and vals["bus"],
                   not vals["air"] and vals["bus"], vals["reset"], not vals["bus"]]
        for r, on in enumerate(current):
            self.check.setItem(r, 2, QTableWidgetItem("◀ hiện tại" if on else ""))
