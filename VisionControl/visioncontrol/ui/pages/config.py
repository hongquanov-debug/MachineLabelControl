"""
Cấu hình:
  * Recipe   : 16 recipes stored in the controller EEPROM (Modbus 40021..40041).
               Ranges = firmware ranges; M2 scale is computed by the controller.
  * Xử lý ảnh: settings.camera1 / camera2 (source, device, ROI, algorithm params)
  * Kết nối  : Modbus TCP + RS485_3 serial port
  * Hệ thống : data folder, shifts, full screen
Settings are saved to config/settings.json; connections and cameras are applied
after restarting the application.
"""
from __future__ import annotations

import json

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (QCheckBox, QComboBox, QDoubleSpinBox, QFormLayout, QGridLayout, QHBoxLayout,
                             QLineEdit, QListWidget, QListWidgetItem, QSpinBox, QTabWidget, QVBoxLayout, QWidget)

from pmems_config.registers import Recipe

from ...controller import AppController
from ..common import confirm, error
from ..widgets import Card, button, label

FIELDS = [  # key, label, unit, min, max, decimals
    ("diameter_mm", "Đường kính lon", "mm", 30, 120, 1),
    ("height_mm", "Chiều cao lon", "mm", 50, 250, 1),
    ("tab_offset_deg", "Góc lệch khoen – tâm tem", "°", -180, 180, 2),
    ("m6_height_mm", "Chiều cao trục nâng M6", "mm", 0, 98, 1),
    ("speed_cans_min", "Tốc độ thiết kế", "lon/ph", 60, 156, 0),
    ("press_ms", "Thời gian ép P5", "ms", 20, 100, 0),
    ("vacuum_detect_ms", "Trễ kiểm tra chân không", "ms", 0, 200, 0),
    ("blow_off_ms", "Thời gian thổi nhả tem", "ms", 0, 100, 0),
    ("m4_overrun_pulses", "Xung chạy thêm M4", "xung", 0, 200, 0),
]


def spin(lo: float, hi: float, dec: int, unit: str):
    s = QDoubleSpinBox() if dec else QSpinBox()
    if dec:
        s.setDecimals(dec)
        s.setSingleStep(10 ** -dec if dec > 1 else 0.1)
    s.setRange(lo, hi)
    s.setSuffix(f"  {unit}")
    s.setButtonSymbols(QSpinBox.ButtonSymbols.NoButtons)
    return s


class ConfigPage(QWidget):
    def __init__(self, ctrl: AppController):
        super().__init__()
        self.ctrl = ctrl
        v = QVBoxLayout(self)
        v.setContentsMargins(10, 4, 10, 10)
        self.tabs = QTabWidget()
        self.tabs.addTab(self._tab_recipe(), "Recipe sản phẩm")
        self.tabs.addTab(self._tab_vision(), "Xử lý ảnh")
        self.tabs.addTab(self._tab_conn(), "Kết nối")
        self.tabs.addTab(self._tab_system(), "Hệ thống")
        v.addWidget(self.tabs)
        ctrl.job_done.connect(self._on_job)
        self._jobs: dict[int, str] = {}
        self.names: dict[int, Recipe] = {}
        self.sel = 1
        self.dirty = False
        self._loading = False

    # ================================================================ recipe tab
    def _tab_recipe(self) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 8, 0, 0)
        h.setSpacing(10)
        lc = Card("Danh sách (1–16)")
        self.list = QListWidget()
        self.list.currentRowChanged.connect(self._pick)
        lc.body.addWidget(self.list)
        lc.setFixedWidth(230)
        h.addWidget(lc)
        fc = Card("Recipe")
        self.title = fc.title
        g = QGridLayout()
        g.setHorizontalSpacing(12)
        g.setVerticalSpacing(4)
        self.name = QLineEdit()
        self.name.setMaxLength(15)
        self.name.textEdited.connect(self._edited)
        g.addWidget(label("Tên sản phẩm (tối đa 15 ký tự)", "hint"), 0, 0, 1, 3)
        g.addWidget(self.name, 1, 0, 1, 3)
        self.inputs = {}
        for i, (k, text, unit, lo, hi, dec) in enumerate(FIELDS):
            r, c = 2 + (i // 3) * 2, i % 3
            g.addWidget(label(f"{text} ({lo:g} – {hi:g})", "hint"), r, c)
            s = spin(lo, hi, dec, unit)
            s.valueChanged.connect(self._edited)
            self.inputs[k] = s
            g.addWidget(s, r + 1, c)
        self.scale = QLineEdit()
        self.scale.setReadOnly(True)
        g.addWidget(label("Hệ số M2 = D / 95 (bộ điều khiển tự tính)", "hint"), 8, 0, 1, 2)
        g.addWidget(self.scale, 9, 0)
        fc.body.addLayout(g)
        fc.body.addStretch(1)
        row = QHBoxLayout()
        reload_ = button("Đọc lại", size="sm")
        reload_.clicked.connect(lambda: self._load(self.sel))
        row.addWidget(reload_)
        self.dirty_lbl = label("● Có thay đổi chưa lưu", "hint")
        self.dirty_lbl.setStyleSheet("color:#f5a524; font-size:12px;")
        self.dirty_lbl.hide()
        row.addWidget(self.dirty_lbl)
        row.addStretch(1)
        save = button("Lưu")
        save.clicked.connect(lambda: self._save(False))
        act = button("Lưu && dùng ngay", "primary")
        act.clicked.connect(lambda: self._save(True))
        row.addWidget(save)
        row.addWidget(act)
        fc.body.addLayout(row)
        h.addWidget(fc, 1)
        return w

    def on_show(self) -> None:
        if not self.names:
            for i in range(1, 17):
                self._jobs[self.ctrl.read_recipe(i)] = "list"

    def _refresh_list(self) -> None:
        self.list.blockSignals(True)
        self.list.clear()
        for i in range(1, 17):
            r = self.names.get(i)
            act = " ●" if i == self.ctrl.active_recipe else ""
            text = f"{i:2d}  {r.name if r else '…'}{act}\n     Ø{r.diameter_mm:.1f} · H{r.height_mm:.1f} mm" if r \
                else f"{i:2d}  …"
            self.list.addItem(QListWidgetItem(text))
        self.list.setCurrentRow(self.sel - 1)
        self.list.blockSignals(False)

    def _pick(self, row: int) -> None:
        if row < 0 or row + 1 == self.sel:
            return
        if self.dirty and not confirm(self, "Bỏ thay đổi?", "Recipe đang sửa có thay đổi chưa lưu."):
            self.list.blockSignals(True)
            self.list.setCurrentRow(self.sel - 1)
            self.list.blockSignals(False)
            return
        self._load(row + 1)

    def _load(self, rid: int) -> None:
        self.sel = rid
        self._jobs[self.ctrl.read_recipe(rid)] = "form"

    def _fill(self, r: Recipe) -> None:
        self._loading = True
        self.title.setText(f"RECIPE #{r.recipe_id} · {r.name}" + ("  (ĐANG DÙNG)" if r.recipe_id == self.ctrl.active_recipe else ""))
        self.name.setText(r.name)
        for k, s in self.inputs.items():
            s.setValue(getattr(r, k))
        self.scale.setText(str(r.m2_scale_x1000 or round(r.diameter_mm * 1000 / 95)))
        self._loading = False
        self._set_dirty(False)

    def _edited(self, *_):
        if self._loading:
            return
        self.scale.setText(str(round(self.inputs["diameter_mm"].value() * 1000 / 95)))
        self._set_dirty(True)

    def _set_dirty(self, d: bool) -> None:
        self.dirty = d
        self.dirty_lbl.setVisible(d)

    def _form_recipe(self) -> Recipe:
        r = Recipe(recipe_id=self.sel, name=self.name.text().strip())
        for k, s in self.inputs.items():
            setattr(r, k, type(getattr(r, k))(s.value()))
        return r

    def _save(self, activate: bool) -> None:
        r = self._form_recipe()
        errs = r.validate()
        if errs:
            error(self, "Giá trị không hợp lệ", "\n".join(errs))
            return
        if activate and not confirm(self, "Lưu và dùng recipe này?",
                                    "Trục M6 sẽ chạy tới chiều cao mới. Chỉ thực hiện khi máy không sản xuất."):
            return
        self._jobs[self.ctrl.save_recipe(r, activate)] = "saved_active" if activate else "saved"

    def _on_job(self, job: int, res, err: str) -> None:
        kind = self._jobs.pop(job, None)
        if kind is None:
            return
        if err:
            if kind.startswith("saved"):
                error(self, "Không lưu được", err)
            return
        if kind in ("list", "form", "saved", "saved_active"):
            self.names[res.recipe_id] = res
            if kind == "saved_active":
                self.ctrl.active_recipe = res.recipe_id
                self.ctrl.recipe_changed.emit()
            if kind != "list" or res.recipe_id == self.sel:
                self._fill(res)
            self._refresh_list()

    # ================================================================ other tabs
    def _tab_vision(self) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 8, 0, 0)
        h.setSpacing(10)
        self.cam_forms = {}
        for idx, cfg, extra in ((1, self.ctrl.cfg.camera1, "MCU chờ 80 ms"), (2, self.ctrl.cfg.camera2, "MCU chờ 150 ms")):
            c = Card(cfg.name, label(extra, "hint"))
            f = QFormLayout()
            src = QComboBox()
            src.addItems(["sim", "external"])
            src.setCurrentText(cfg.source)
            dev = QLineEdit(cfg.device)
            roi = QLineEdit(",".join(map(str, cfg.roi)) if cfg.roi else "")
            roi.setPlaceholderText("x,y,w,h (trống = toàn ảnh)")
            ng = QCheckBox("Lưu ảnh NG")
            ng.setChecked(cfg.save_ng_images)
            params = QLineEdit(json.dumps(cfg.params, ensure_ascii=False))
            f.addRow("Nguồn ảnh", src)
            f.addRow("Thiết bị", dev)
            f.addRow("ROI", roi)
            f.addRow("", ng)
            f.addRow("Tham số thuật toán (JSON)", params)
            c.body.addLayout(f)
            c.body.addWidget(label("“sim” = giả lập. “external” = camera + thuật toán thật "
                                   "(vision/camera_source.py, angle_detector.py, code_reader.py).", "hint", wrap=True))
            c.body.addStretch(1)
            self.cam_forms[idx] = (cfg, src, dev, roi, ng, params)
            h.addWidget(c)
        return self._with_save(w)

    def _tab_conn(self) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 8, 0, 0)
        h.setSpacing(10)
        p = self.ctrl.cfg.plc
        c1 = Card("Ethernet – Modbus TCP")
        f1 = QFormLayout()
        self.host = QLineEdit(p.host)
        self.port = QSpinBox()
        self.port.setRange(1, 65535)
        self.port.setValue(p.port)
        self.poll = QSpinBox()
        self.poll.setRange(50, 5000)
        self.poll.setSuffix(" ms")
        self.poll.setValue(p.poll_ms)
        f1.addRow("IP bộ điều khiển", self.host)
        f1.addRow("Cổng", self.port)
        f1.addRow("Chu kỳ đọc", self.poll)
        c1.body.addLayout(f1)
        c1.body.addStretch(1)
        c2 = Card("RS485_3 – kết quả Vision")
        f2 = QFormLayout()
        self.com = QLineEdit(self.ctrl.cfg.serial.port)
        baud = QLineEdit(str(self.ctrl.cfg.serial.baud))
        baud.setReadOnly(True)
        f2.addRow("Cổng COM", self.com)
        f2.addRow("Baud (8N1, theo firmware)", baud)
        c2.body.addLayout(f2)
        c2.body.addStretch(1)
        h.addWidget(c1)
        h.addWidget(c2)
        return self._with_save(w)

    def _tab_system(self) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 8, 0, 0)
        c = Card("Chung")
        f = QFormLayout()
        self.data_dir = QLineEdit(self.ctrl.cfg.data_dir)
        self.shifts = QLineEdit(", ".join(self.ctrl.cfg.shifts))
        self.full = QCheckBox("Toàn màn hình khi khởi động")
        self.full.setChecked(self.ctrl.cfg.fullscreen)
        f.addRow("Thư mục dữ liệu", self.data_dir)
        f.addRow("Giờ bắt đầu ca", self.shifts)
        f.addRow("", self.full)
        c.body.addLayout(f)
        c.body.addStretch(1)
        h.addWidget(c)
        h.addStretch(1)
        return self._with_save(w)

    def _with_save(self, w: QWidget) -> QWidget:
        outer = QWidget()
        v = QVBoxLayout(outer)
        v.setContentsMargins(0, 0, 0, 0)
        v.addWidget(w, 1)
        row = QHBoxLayout()
        row.addWidget(label("Lưu vào config/settings.json. Kết nối và camera áp dụng sau khi khởi động lại.", "hint"))
        row.addStretch(1)
        b = button("Lưu cấu hình", "primary")
        b.clicked.connect(self._save_settings)
        row.addWidget(b)
        v.addLayout(row)
        return outer

    def _save_settings(self) -> None:
        cfg = self.ctrl.cfg
        try:
            for _idx, (cam, src, dev, roi, ng, params) in self.cam_forms.items():
                cam.source = src.currentText()
                cam.device = dev.text().strip()
                cam.roi = [int(x) for x in roi.text().split(",")] if roi.text().strip() else None
                if cam.roi is not None and len(cam.roi) != 4:
                    raise ValueError("ROI phải có 4 số: x,y,w,h")
                cam.save_ng_images = ng.isChecked()
                cam.params = json.loads(params.text() or "{}")
            cfg.plc.host, cfg.plc.port, cfg.plc.poll_ms = self.host.text().strip(), self.port.value(), self.poll.value()
            cfg.serial.port = self.com.text().strip()
            cfg.data_dir = self.data_dir.text().strip() or "data"
            cfg.shifts = [s.strip() for s in self.shifts.text().split(",") if s.strip()]
            cfg.fullscreen = self.full.isChecked()
        except (ValueError, json.JSONDecodeError) as e:
            error(self, "Cấu hình không hợp lệ", str(e))
            return
        self.ctrl.save_settings()
        confirm(self, "Đã lưu", "Đã lưu cấu hình. Khởi động lại phần mềm để áp dụng kết nối và camera.")
