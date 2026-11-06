"""
Camera: camera 1 (tab angle) / camera 2 (DataMatrix) selector, live image with
overlay, result panel, statistics, history.

Image modes: live (every result), last (frozen), NG (last NG image).
The page repaints at most every 100 ms whatever the camera rate.
"""
from __future__ import annotations

from collections import deque

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtWidgets import (QGridLayout, QHBoxLayout, QHeaderView, QStackedWidget, QTableWidget,
                             QTableWidgetItem, QVBoxLayout, QWidget)

from ...controller import AppController
from ...core.models import VisionRecord
from ..theme import C
from ..widgets import Card, ImageView, Segmented, Stat, button, label, set_css

GRADE = {4: "A", 3: "B", 2: "C", 1: "D", 0: "F"}
HIST = 100


class CameraPage(QWidget):
    def __init__(self, ctrl: AppController):
        super().__init__()
        self.ctrl = ctrl
        self.cam = 1
        self.mode = "live"
        self.frozen = False
        self._dirty = True
        self._shown = False
        self.hist = {1: deque(maxlen=HIST), 2: deque(maxlen=HIST)}

        g = QGridLayout(self)
        g.setContentsMargins(10, 10, 10, 10)
        g.setSpacing(10)
        # ---- image card
        top = QHBoxLayout()
        self.cam_sel = Segmented([("1", "Cam 1 · Góc"), ("2", "Cam 2 · DataMatrix")])
        self.mode_sel = Segmented([("live", "Trực tiếp"), ("last", "Ảnh cuối"), ("ng", "Ảnh lỗi")])
        top.addWidget(self.cam_sel)
        top.addStretch(1)
        top.addWidget(self.mode_sel)
        img_card = Card()
        img_card.body.addLayout(top)
        self.view = ImageView()
        img_card.body.addWidget(self.view, 1)
        g.addWidget(img_card, 0, 0)
        # ---- side panels (one per camera)
        self.side = QStackedWidget()
        self.side.addWidget(self._side_cam1())
        self.side.addWidget(self._side_cam2())
        self.side.setFixedWidth(292)
        g.addWidget(self.side, 0, 1, 2, 1)
        # ---- history
        hc = Card("Lịch sử")
        self.hist_note = label("", "hint")
        self.table = QTableWidget(0, 6)
        self.table.verticalHeader().hide()
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setSelectionMode(QTableWidget.SelectionMode.NoSelection)
        self.table.setAlternatingRowColors(True)
        hc.body.addWidget(self.table)
        hc.setFixedHeight(178)
        g.addWidget(hc, 1, 0)
        g.setColumnStretch(0, 1)
        g.setRowStretch(0, 1)

        self.cam_sel.changed.connect(self._select_cam)
        self.mode_sel.changed.connect(self._select_mode)
        ctrl.vision_result.connect(self._on_result)
        t = QTimer(self)
        t.timeout.connect(self._repaint)
        t.start(100)
        self._select_cam("1")

    # ---------------------------------------------------------------- side panels
    def _side_cam1(self) -> QWidget:
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(10)
        c = Card("Kết quả nhận diện", label("0x20", "hint"))
        self.c1_theta = label("–", "bigValue")
        self.c1_theta.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.c1_q = label("–", "mono")
        self.c1_q.setAlignment(Qt.AlignmentFlag.AlignCenter)
        c.body.addWidget(self.c1_theta)
        c.body.addWidget(self.c1_q)
        grid = QGridLayout()
        grid.setSpacing(6)
        self.c1 = {k: Stat(t, col) for k, t, col in (("ms", "Thời gian (trigger→gửi)", ""),
                                                     ("avg", "Trung bình", ""), ("ok", "Đạt", "ok"),
                                                     ("ng", "Không đạt", "err"), ("late", "Trễ > 80 ms", "warn"),
                                                     ("crc", "Lỗi CRC RS485", ""))}
        for i, s in enumerate(self.c1.values()):
            grid.addWidget(s, i // 2, i % 2)
        c.body.addLayout(grid)
        v.addWidget(c)
        p = Card("Thông số")
        self.c1_params = label("", "muted", wrap=True)
        p.body.addWidget(self.c1_params)
        p.body.addStretch(1)
        p.body.addWidget(label("Sửa ở Cấu hình → Xử lý ảnh", "hint"))
        v.addWidget(p, 1)
        return w

    def _side_cam2(self) -> QWidget:
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(10)
        c = Card("Kết quả đọc mã", label("0x21", "hint"))
        self.c2_res = label("–", "bigValue")
        self.c2_res.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.c2_q = label("–", "mono")
        self.c2_q.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.c2_data = label("–", "dataBox", wrap=True)
        self.c2_data.setMinimumHeight(48)
        c.body.addWidget(self.c2_res)
        c.body.addWidget(self.c2_q)
        c.body.addWidget(self.c2_data)
        grid = QGridLayout()
        grid.setSpacing(6)
        self.c2 = {k: Stat(t, col) for k, t, col in (("grade", "Grade in", ""), ("ms", "Thời gian", ""),
                                                     ("ok", "Đạt", "ok"), ("ng", "Không đạt", "err"),
                                                     ("late", "Trễ > 150 ms", "warn"), ("dup", "Trùng mã", "warn"))}
        for i, s in enumerate(self.c2.values()):
            grid.addWidget(s, i // 2, i % 2)
        c.body.addLayout(grid)
        v.addWidget(c)
        p = Card("Quy tắc kiểm tra")
        self.c2_params = label("", "muted", wrap=True)
        p.body.addWidget(self.c2_params)
        p.body.addStretch(1)
        p.body.addWidget(label("Sửa ở Cấu hình → Xử lý ảnh", "hint"))
        v.addWidget(p, 1)
        return w

    # ---------------------------------------------------------------- events
    def _select_cam(self, key: str) -> None:
        self.cam = int(key)
        self.side.setCurrentIndex(self.cam - 1)
        self.frozen = False
        cfg = self.ctrl.cfg.camera1 if self.cam == 1 else self.ctrl.cfg.camera2
        text = "\n".join(f"{k}: {v}" for k, v in cfg.params.items()) or "–"
        (self.c1_params if self.cam == 1 else self.c2_params).setText(f"Nguồn: {cfg.source}\n{text}")
        if self.cam == 1:
            self.table.setColumnCount(6)
            self.table.setHorizontalHeaderLabels(["Giờ", "CAN_ID", "θ (°)", "QUALITY", "ms", "Ghi chú"])
            self.hist_note.setText("gói 0x20 gửi về MCU")
        else:
            self.table.setColumnCount(6)
            self.table.setHorizontalHeaderLabels(["Giờ", "CAN_ID", "Dữ liệu", "Grade", "QUALITY", "ms"])
        hh = self.table.horizontalHeader()
        hh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
        hh.setSectionResizeMode(2 if self.cam == 2 else 5, QHeaderView.ResizeMode.Stretch)
        self._fill_table()
        self._dirty = True

    def _select_mode(self, key: str) -> None:
        self.mode = key
        self.frozen = False
        self._dirty = True

    def _on_result(self, rec: VisionRecord) -> None:
        self.hist[rec.camera].appendleft(rec)
        if rec.camera == self.cam:
            self._add_row(rec, 0)
            while self.table.rowCount() > HIST:
                self.table.removeRow(self.table.rowCount() - 1)
            self._dirty = True

    def _fill_table(self) -> None:
        self.table.setRowCount(0)
        for rec in self.hist[self.cam]:
            self._add_row(rec, self.table.rowCount())

    def _add_row(self, r: VisionRecord, row: int) -> None:
        self.table.insertRow(row)
        if r.camera == 1:
            cells = [r.ts.strftime("%H:%M:%S"), str(r.can_id), "–" if r.theta_deg is None else f"{r.theta_deg:.2f}",
                     r.quality_name, f"{r.elapsed_ms:.0f}", r.reason]
            qcol = 3
        else:
            cells = [r.ts.strftime("%H:%M:%S"), str(r.can_id), r.data or "—",
                     GRADE.get(r.grade, "–") if r.grade is not None else "–", r.quality_name, f"{r.elapsed_ms:.0f}"]
            qcol = 4
        for j, t in enumerate(cells):
            it = QTableWidgetItem(t)
            if j == qcol:
                it.setForeground(Qt.GlobalColor.green if r.ok else Qt.GlobalColor.red)
            if r.late and j == len(cells) - 1:
                it.setForeground(Qt.GlobalColor.yellow)
            self.table.setItem(row, j, it)

    # ---------------------------------------------------------------- repaint (≤ 10 Hz)
    def _repaint(self) -> None:
        if not self._dirty or not self._shown:
            return
        self._dirty = False
        c = self.ctrl
        rec = c.last_ng[self.cam] if self.mode == "ng" else c.last_record[self.cam]
        if not (self.mode == "last" and self.frozen):
            if rec is None:
                self.view.set_frame(None)
            else:
                left = f"CAN #{rec.can_id}\n" + (f"θ = {rec.theta_deg:+.2f}°" if rec.theta_deg is not None
                                                  else rec.quality_name)
                self.view.set_frame(rec.image, rec.overlay, left, f"{rec.elapsed_ms:.1f} ms")
            self.frozen = self.mode == "last" and rec is not None
        last, s = c.last_record[self.cam], c.stats[self.cam]
        if self.cam == 1:
            self.c1["avg"].set(f"{s.avg_ms:.1f} ms")
            self.c1["ok"].set(str(s.ok))
            self.c1["ng"].set(str(s.ng))
            self.c1["late"].set(str(s.late))
            self.c1["crc"].set(str(getattr(c.link, "crc_errors", 0)))
            if last:
                self.c1_theta.setText(f"{last.theta_deg:+.2f}°" if last.theta_deg is not None else "NG")
                set_css(self.c1_theta, f"color:{C['text'] if last.ok else C['err']};")
                self.c1_q.setText(f"QUALITY {last.quality:#04x} {last.quality_name}")
                self.c1["ms"].set(f"{last.elapsed_ms:.1f} ms")
        else:
            self.c2["ok"].set(str(s.ok))
            self.c2["ng"].set(str(s.ng))
            self.c2["late"].set(str(s.late))
            self.c2["dup"].set(str(s.duplicate))
            if last:
                self.c2_res.setText("ĐẠT" if last.ok else "LỖI")
                set_css(self.c2_res, f"color:{C['ok'] if last.ok else C['err']};")
                self.c2_q.setText(f"QUALITY {last.quality:#04x} {last.quality_name}")
                self.c2_data.setText(last.data or "— không có dữ liệu —")
                self.c2["grade"].set(GRADE.get(last.grade, "–") if last.grade is not None else "–")
                self.c2["ms"].set(f"{last.elapsed_ms:.1f} ms")

    def on_show(self) -> None:
        self._shown = True
        self._dirty = True
        self._repaint()

    def showEvent(self, e) -> None:
        self._shown = True
        self._dirty = True
        super().showEvent(e)

    def hideEvent(self, e) -> None:
        self._shown = False
        super().hideEvent(e)
