"""
Nhật ký: every LogEntry (FSM changes, alarms, vision NG, communication) with
category filter, text search, pause scrolling, CSV export and alarm reset.
Model/view: LogModel (QAbstractTableModel) + LogFilter (QSortFilterProxyModel).
"""
from __future__ import annotations

import csv

from PyQt6.QtCore import QAbstractTableModel, QModelIndex, QSortFilterProxyModel, Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import QFileDialog, QHBoxLayout, QHeaderView, QLineEdit, QTableView, QVBoxLayout, QWidget

from pmems_config.registers import MachineCmd

from ...controller import AppController
from ...core.models import LogEntry
from ..theme import C
from ..widgets import Segmented, button, label

LEVEL = {"err": ("Lỗi", C["err"]), "warn": ("Cảnh báo", C["warn"]), "info": ("Thông tin", C["info"])}
HEAD = ["Thời gian", "Mức", "Nguồn", "Mã", "Nội dung"]
MAX_ROWS = 2000


class LogModel(QAbstractTableModel):
    def __init__(self):
        super().__init__()
        self.rows: list[LogEntry] = []

    def rowCount(self, parent=QModelIndex()):
        return len(self.rows)

    def columnCount(self, parent=QModelIndex()):
        return len(HEAD)

    def headerData(self, s, o, role=Qt.ItemDataRole.DisplayRole):
        if role == Qt.ItemDataRole.DisplayRole and o == Qt.Orientation.Horizontal:
            return HEAD[s]
        return None

    def data(self, idx, role=Qt.ItemDataRole.DisplayRole):
        e = self.rows[idx.row()]
        col = idx.column()
        if role == Qt.ItemDataRole.DisplayRole:
            return [e.ts.strftime("%d/%m %H:%M:%S"), LEVEL[e.level][0], e.source, e.code, e.message][col]
        if role == Qt.ItemDataRole.ForegroundRole and col == 1:
            return QColor(LEVEL[e.level][1])
        if role == Qt.ItemDataRole.UserRole:
            return e
        return None

    def add_front(self, entries: list[LogEntry]) -> None:
        if not entries:
            return
        self.beginInsertRows(QModelIndex(), 0, len(entries) - 1)
        self.rows[0:0] = entries
        self.endInsertRows()
        if len(self.rows) > MAX_ROWS:
            self.beginRemoveRows(QModelIndex(), MAX_ROWS, len(self.rows) - 1)
            del self.rows[MAX_ROWS:]
            self.endRemoveRows()


class LogFilter(QSortFilterProxyModel):
    def __init__(self):
        super().__init__()
        self.category = "all"
        self.text = ""

    def set(self, category: str | None = None, text: str | None = None) -> None:
        if category is not None:
            self.category = category
        if text is not None:
            self.text = text.lower()
        self.invalidateFilter()

    def filterAcceptsRow(self, row, parent):
        e: LogEntry = self.sourceModel().rows[row]
        if self.category != "all" and e.category != self.category:
            return False
        return not self.text or self.text in f"{e.message} {e.code} {e.source}".lower()


class LogPage(QWidget):
    def __init__(self, ctrl: AppController):
        super().__init__()
        self.ctrl = ctrl
        v = QVBoxLayout(self)
        v.setContentsMargins(10, 10, 10, 10)
        top = QHBoxLayout()
        self.cat = Segmented([("all", "Tất cả"), ("alarm", "Alarm"), ("event", "Sự kiện"),
                              ("vision", "Vision"), ("comm", "Truyền thông")])
        top.addWidget(self.cat)
        self.search = QLineEdit()
        self.search.setPlaceholderText("Tìm kiếm nội dung, mã alarm…")
        self.search.setFixedWidth(260)
        top.addWidget(self.search)
        top.addStretch(1)
        self.pause = button("Tạm dừng cuộn", size="sm")
        self.pause.setCheckable(True)
        top.addWidget(self.pause)
        exp = button("Xuất CSV", size="sm")
        exp.clicked.connect(self.export)
        top.addWidget(exp)
        v.addLayout(top)
        self.model = LogModel()
        self.proxy = LogFilter()
        self.proxy.setSourceModel(self.model)
        self.view = QTableView()
        self.view.setModel(self.proxy)
        self.view.verticalHeader().hide()
        self.view.setAlternatingRowColors(True)
        self.view.setSelectionBehavior(QTableView.SelectionBehavior.SelectRows)
        hh = self.view.horizontalHeader()
        for i, w in enumerate((130, 80, 80, 70)):
            self.view.setColumnWidth(i, w)
        hh.setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
        v.addWidget(self.view, 1)
        foot = QHBoxLayout()
        self.counts = label("", "muted")
        foot.addWidget(self.counts)
        foot.addStretch(1)
        ack = button("Xác nhận / xóa alarm", size="sm")
        ack.clicked.connect(lambda: ctrl.command(MachineCmd.RESET_ALARM))
        foot.addWidget(ack)
        v.addLayout(foot)
        self._pending: list[LogEntry] = []
        self.cat.changed.connect(lambda k: (self.proxy.set(category=k), self._count()))
        self.search.textChanged.connect(lambda t: (self.proxy.set(text=t), self._count()))
        self.pause.toggled.connect(self._toggle_pause)
        ctrl.log_added.connect(self._on_log)
        self.model.add_front(list(ctrl.log))
        self._count()

    def _on_log(self, e: LogEntry) -> None:
        self._pending.insert(0, e)
        if not self.pause.isChecked():
            self._flush()

    def _flush(self) -> None:
        self.model.add_front(self._pending)
        self._pending = []
        self._count()

    def _toggle_pause(self, on: bool) -> None:
        self.pause.setText("Tiếp tục cuộn" if on else "Tạm dừng cuộn")
        if not on:
            self._flush()

    def _count(self) -> None:
        rows = self.model.rows
        n = {k: sum(1 for e in rows if e.level == k) for k in LEVEL}
        self.counts.setText(f"Hiển thị {self.proxy.rowCount()} dòng   ·   Lỗi {n['err']}   ·   "
                            f"Cảnh báo {n['warn']}   ·   Thông tin {n['info']}")

    def export(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Xuất nhật ký", "nhat_ky.csv", "CSV (*.csv)")
        if not path:
            return
        with open(path, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(HEAD)
            for r in range(self.proxy.rowCount()):
                e = self.proxy.data(self.proxy.index(r, 0), Qt.ItemDataRole.UserRole)
                w.writerow([e.ts.isoformat(sep=" "), e.level, e.source, e.code, e.message])
