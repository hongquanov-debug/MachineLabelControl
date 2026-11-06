"""
Báo cáo: totals, hourly output chart, NG reasons, alarm stops, CSV export.
Data = HistoryDB (production sampled every minute from the controller counters,
vision results, events). Shift filter uses settings.shifts start times.
"""
from __future__ import annotations

import csv
from datetime import date, datetime, time, timedelta

from PyQt6.QtCore import QDate
from PyQt6.QtWidgets import (QComboBox, QDateEdit, QFileDialog, QGridLayout, QHBoxLayout, QHeaderView,
                             QProgressBar, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget)

from ...controller import AppController
from ..widgets import BarChart, Card, KpiCard, button, label


class ReportPage(QWidget):
    def __init__(self, ctrl: AppController):
        super().__init__()
        self.ctrl = ctrl
        g = QGridLayout(self)
        g.setContentsMargins(10, 10, 10, 10)
        g.setSpacing(10)
        bar = QHBoxLayout()
        today = QDate.currentDate()
        self.d_from, self.d_to = QDateEdit(today), QDateEdit(today)
        for d in (self.d_from, self.d_to):
            d.setCalendarPopup(True)
            d.setDisplayFormat("dd/MM/yyyy")
        self.shift = QComboBox()
        self.shift.addItem("Cả ngày")
        for i, s in enumerate(ctrl.cfg.shifts):
            self.shift.addItem(f"Ca {i + 1} (từ {s})")
        for t, w in (("Từ ngày", self.d_from), ("Đến ngày", self.d_to), ("Ca", self.shift)):
            v = QVBoxLayout()
            v.setSpacing(2)
            v.addWidget(label(t, "hint"))
            v.addWidget(w)
            bar.addLayout(v)
        show = button("Xem", "primary")
        show.clicked.connect(self.refresh)
        bar.addWidget(show)
        bar.addStretch(1)
        exp = button("Xuất CSV")
        exp.clicked.connect(self.export)
        bar.addWidget(exp)
        g.addLayout(bar, 0, 0, 1, 2)
        k = QHBoxLayout()
        self.k = {key: KpiCard(t, "", col) for key, t, col in (
            ("fed", "Tổng lon", ""), ("ok", "Đạt", "ok"), ("ng", "Lỗi", "err"), ("yield", "Tỉ lệ đạt", ""),
            ("run", "Thời gian chạy", ""), ("stops", "Số lần dừng", "warn"))}
        for w in self.k.values():
            k.addWidget(w)
        g.addLayout(k, 1, 0, 1, 2)
        cc = Card("Sản lượng theo giờ", label("■ đạt  ■ lỗi", "hint"))
        self.chart = BarChart()
        cc.body.addWidget(self.chart, 1)
        g.addWidget(cc, 2, 0)
        side = Card("Nguyên nhân lỗi")
        self.reasons = QVBoxLayout()
        self.reasons.setSpacing(6)
        side.body.addLayout(self.reasons)
        side.body.addWidget(label("DỪNG MÁY", "cardTitle"))
        self.stops = QTableWidget(0, 3)
        self.stops.setHorizontalHeaderLabels(["Mã", "Nội dung", "Lần"])
        self.stops.verticalHeader().hide()
        self.stops.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        side.body.addWidget(self.stops, 1)
        side.setFixedWidth(300)
        g.addWidget(side, 2, 1)
        g.setRowStretch(2, 1)
        g.setColumnStretch(0, 1)
        self._rows: list = []

    def _range(self) -> tuple[datetime, datetime]:
        d0: date = self.d_from.date().toPyDate()
        d1: date = self.d_to.date().toPyDate()
        start, end = datetime.combine(d0, time()), datetime.combine(d1, time()) + timedelta(days=1)
        i = self.shift.currentIndex()
        if i > 0 and d0 == d1:                         # one shift of one day
            shifts = self.ctrl.cfg.shifts
            h0 = datetime.strptime(shifts[i - 1], "%H:%M").time()
            start = datetime.combine(d0, h0)
            h1 = datetime.strptime(shifts[i % len(shifts)], "%H:%M").time()
            end = datetime.combine(d0 if i < len(shifts) else d0 + timedelta(days=1), h1)
        return start, end

    def on_show(self) -> None:
        self.refresh()

    def refresh(self) -> None:
        start, end = self._range()
        h = self.ctrl.history
        t = h.totals(start, end)
        fed, ok, ng = int(t["fed"]), int(t["ok"]), int(t["ng"])
        fmt = lambda n: f"{n:,}".replace(",", ".")
        self.k["fed"].set(fmt(fed))
        self.k["ok"].set(fmt(ok))
        self.k["ng"].set(fmt(ng))
        self.k["yield"].set(f"{ok / (ok + ng) * 100:.2f} %" if ok + ng else "–")
        run = int(t["run_s"])
        self.k["run"].set(f"{run // 3600:02d}:{run % 3600 // 60:02d}")
        alarms = h.alarm_counts(start, end)
        self.k["stops"].set(str(sum(a[2] for a in alarms)))
        self._rows = h.hourly(start, end)
        self.chart.set_data([r[0][-2:] + "h" for r in self._rows], [r[2] for r in self._rows],
                            [r[3] for r in self._rows])
        while self.reasons.count():
            it = self.reasons.takeAt(0)
            if it.widget():
                it.widget().deleteLater()
            elif it.layout():
                while it.layout().count():
                    w = it.layout().takeAt(0).widget()
                    if w:
                        w.deleteLater()
        reasons = h.ng_reasons(start, end)[:6]
        top = max((n for _, n in reasons), default=1)
        if not reasons:
            self.reasons.addWidget(label("Không có lỗi", "muted"))
        for text, n in reasons:
            row = QHBoxLayout()
            row.addWidget(label(text))
            row.addStretch(1)
            row.addWidget(label(str(n), "mono"))
            self.reasons.addLayout(row)
            bar = QProgressBar()
            bar.setProperty("kind", "err")
            bar.setTextVisible(False)
            bar.setMaximum(top)
            bar.setValue(n)
            self.reasons.addWidget(bar)
        self.stops.setRowCount(len(alarms))
        for r, (code, msg, n) in enumerate(alarms):
            for c, v in enumerate((code, msg, str(n))):
                self.stops.setItem(r, c, QTableWidgetItem(v))

    def export(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Xuất báo cáo", "bao_cao.csv", "CSV (*.csv)")
        if not path:
            return
        with open(path, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(["Giờ", "Tổng", "Đạt", "Lỗi"])
            w.writerows(self._rows)
