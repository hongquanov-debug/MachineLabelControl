"""Reusable widgets (all painted with QPainter / styled by theme.QSS)."""
from __future__ import annotations

from typing import Iterable, Optional, Sequence

import numpy as np
from PyQt6.QtCore import QPointF, QRectF, Qt, pyqtSignal
from PyQt6.QtGui import QBrush, QColor, QFont, QImage, QPainter, QPen, QPolygonF
from PyQt6.QtWidgets import (QFrame, QHBoxLayout, QLabel, QPushButton, QSizePolicy, QVBoxLayout,
                             QWidget)

from ..core.can_tracker import (POS_CAM1, POS_CAM2, POS_END, POS_LABEL, POS_ORIENT, POS_REJECT, POS_S5,
                                ST_ANGLE_OK, ST_LABELED, ST_NG, ST_PASS, TrackedCan)
from ..core.models import OvCircle, OvLine, OvPolygon, OvText
from .theme import C, repolish


def label(text: str = "", obj: str = "", wrap: bool = False) -> QLabel:
    lb = QLabel(text)
    if obj:
        lb.setObjectName(obj)
    lb.setWordWrap(wrap)
    return lb


def button(text: str, kind: str = "", size: str = "", big: bool = False) -> QPushButton:
    b = QPushButton(text)
    if kind:
        b.setProperty("kind", kind)
    if size:
        b.setProperty("size", size)
    if big:
        b.setProperty("big", True)
    b.setCursor(Qt.CursorShape.PointingHandCursor)
    return b


def set_css(w: QWidget, css: str) -> None:
    """setStyleSheet only when it changes (re-polishing is expensive at 4 Hz × hundreds of widgets)."""
    if w.styleSheet() != css:
        w.setStyleSheet(css)


def set_kind(w: QWidget, kind: str) -> None:
    if w.property("kind") != kind:
        w.setProperty("kind", kind)
        repolish(w)


class Card(QFrame):
    """Panel with an upper-case title and an optional widget on the right of the title."""

    def __init__(self, title: str = "", right: Optional[QWidget] = None, spacing: int = 8):
        super().__init__()
        self.setObjectName("card")
        v = QVBoxLayout(self)
        v.setContentsMargins(10, 8, 10, 10)
        v.setSpacing(spacing)
        if title or right:
            h = QHBoxLayout()
            h.setContentsMargins(0, 0, 0, 0)
            self.title = label(title.upper(), "cardTitle")
            h.addWidget(self.title)
            h.addStretch(1)
            if right is not None:
                h.addWidget(right)
            v.addLayout(h)
        self.body = v


class Led(QWidget):
    COLORS = {"off": C["off"], "ok": C["ok"], "err": C["err"], "warn": C["warn"], "info": C["info"]}

    def __init__(self, size: int = 12):
        super().__init__()
        self._c = "off"
        self.setFixedSize(size, size)

    def set(self, color: str) -> None:
        if color != self._c:
            self._c = color
            self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        col = QColor(self.COLORS.get(self._c, self._c))
        if self._c != "off":
            glow = QColor(col)
            glow.setAlpha(70)
            p.setBrush(glow)
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(self.rect())
        p.setBrush(col)
        p.setPen(Qt.PenStyle.NoPen)
        r = self.rect().adjusted(2, 2, -2, -2)
        p.drawEllipse(r)


class LedLabel(QWidget):
    """LED + text in a rounded box (I/O lists, safety summary)."""

    def __init__(self, text: str, code: str = ""):
        super().__init__()
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.setStyleSheet(f"background:{C['panel2']}; border-radius:4px;")
        h = QHBoxLayout(self)
        h.setContentsMargins(6, 3, 6, 3)
        h.setSpacing(6)
        self.led = Led()
        h.addWidget(self.led)
        if code:
            c = label(code, "hint")
            c.setFixedWidth(24)
            h.addWidget(c)
        self.text = label(text)
        self.text.setStyleSheet("font-size:12px; background:transparent;")
        h.addWidget(self.text, 1)

    def set(self, color: str, bad: bool = False) -> None:
        self.led.set(color)
        set_css(self, f"background:{'rgba(239,68,68,0.15)' if bad else C['panel2']}; border-radius:4px;")


class KpiCard(QFrame):
    def __init__(self, title: str, sub: str = "", color: str = ""):
        super().__init__()
        self.setObjectName("kpi")
        v = QVBoxLayout(self)
        v.setContentsMargins(12, 6, 12, 6)
        v.setSpacing(0)
        v.addWidget(label(title.upper(), "kpiTitle"))
        self.value = label("–", "kpiValue")
        if color:
            self.value.setStyleSheet(f"color:{C[color]};")
        v.addWidget(self.value)
        self.sub = label(sub, "kpiSub")
        v.addWidget(self.sub)

    def set(self, value: str, sub: Optional[str] = None) -> None:
        self.value.setText(value)
        if sub is not None:
            self.sub.setText(sub)


class Stat(QFrame):
    def __init__(self, title: str, color: str = ""):
        super().__init__()
        self.setObjectName("stat")
        v = QVBoxLayout(self)
        v.setContentsMargins(8, 4, 8, 4)
        v.setSpacing(0)
        v.addWidget(label(title, "hint"))
        self.value = label("–", "statValue")
        if color:
            self.value.setStyleSheet(f"color:{C[color]};")
        v.addWidget(self.value)

    def set(self, text: str) -> None:
        self.value.setText(text)


class Segmented(QWidget):
    """Exclusive button group: changed(key)."""
    changed = pyqtSignal(str)

    def __init__(self, items: Sequence[tuple[str, str]], current: Optional[str] = None):
        super().__init__()
        h = QHBoxLayout(self)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(0)
        self.buttons: dict[str, QPushButton] = {}
        for key, text in items:
            b = QPushButton(text)
            b.setObjectName("seg")
            b.setCheckable(True)
            b.clicked.connect(lambda _=False, k=key: self.select(k))
            h.addWidget(b)
            self.buttons[key] = b
        self.current = current or items[0][0]
        self.buttons[self.current].setChecked(True)

    def select(self, key: str) -> None:
        for k, b in self.buttons.items():
            b.setChecked(k == key)
        if key != self.current:
            self.current = key
            self.changed.emit(key)


def numpy_to_qimage(img: np.ndarray) -> QImage:
    img = np.ascontiguousarray(img)
    if img.ndim == 2:
        q = QImage(img.data, img.shape[1], img.shape[0], img.strides[0], QImage.Format.Format_Grayscale8)
    else:
        q = QImage(img.data, img.shape[1], img.shape[0], img.strides[0], QImage.Format.Format_BGR888)
    return q.copy()       # own the memory (numpy buffer may be reused)


class ImageView(QWidget):
    """Camera image scaled to fit (aspect kept) + overlay primitives in image coordinates."""

    def __init__(self, placeholder: str = "Chờ ảnh…"):
        super().__init__()
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMinimumSize(120, 90)
        self.placeholder = placeholder
        self._img: Optional[QImage] = None
        self._ov: list = []
        self._osd_left = self._osd_right = ""

    def set_frame(self, image: Optional[np.ndarray], overlay: Iterable = (), osd_left: str = "", osd_right: str = ""):
        self._img = numpy_to_qimage(image) if image is not None else None
        self._ov = list(overlay)
        self._osd_left, self._osd_right = osd_left, osd_right
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QColor("#050607"))
        if self._img is None:
            p.setPen(QColor(C["muted"]))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, self.placeholder)
            return
        iw, ih = self._img.width(), self._img.height()
        s = min(self.width() / iw, self.height() / ih)
        ox, oy = (self.width() - iw * s) / 2, (self.height() - ih * s) / 2
        p.drawImage(QRectF(ox, oy, iw * s, ih * s), self._img)
        p.save()
        p.translate(ox, oy)
        p.scale(s, s)
        for o in self._ov:
            pen = QPen(QColor(o.color))
            pen.setCosmetic(True)
            pen.setWidthF(getattr(o, "width", 2.0))
            if getattr(o, "dashed", False):
                pen.setStyle(Qt.PenStyle.DashLine)
            p.setPen(pen)
            p.setBrush(Qt.BrushStyle.NoBrush)
            if isinstance(o, OvCircle):
                p.drawEllipse(QPointF(o.x, o.y), o.r, o.r)
            elif isinstance(o, OvLine):
                p.drawLine(QPointF(o.x1, o.y1), QPointF(o.x2, o.y2))
            elif isinstance(o, OvPolygon):
                p.drawPolygon(QPolygonF([QPointF(x, y) for x, y in o.points]))
            elif isinstance(o, OvText):
                p.save()
                p.resetTransform()
                f = QFont("DejaVu Sans Mono", 10)
                f.setBold(True)
                p.setFont(f)
                p.drawText(QPointF(ox + o.x * s, oy + o.y * s), o.text)
                p.restore()
        p.restore()
        f = QFont("DejaVu Sans Mono", 9)
        p.setFont(f)
        p.setPen(QColor("#9fe6b5"))
        r = self.rect().adjusted(8, 6, -8, -6)
        p.drawText(r, Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop, self._osd_left)
        p.drawText(r, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignTop, self._osd_right)


class ConveyorView(QWidget):
    STATIONS = [(0, "S2"), (POS_CAM1, "Cam 1"), (POS_ORIENT, "Chỉnh góc"), (POS_LABEL, "Dán tem"),
                (POS_S5, "S5"), (POS_CAM2, "Cam 2"), (POS_REJECT, "Loại P8"), (POS_END, "Ra")]
    COLORS = {"wait": C["info"], ST_ANGLE_OK: C["accent"], ST_LABELED: C["ok"], ST_PASS: C["ok"], ST_NG: C["err"]}

    def __init__(self):
        super().__init__()
        self.setMinimumHeight(110)
        self.cans: list[TrackedCan] = []
        self.offset = 0.0

    def set_cans(self, cans: list[TrackedCan], belt_mm: float) -> None:
        self.cans = cans
        self.offset = belt_mm
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        x0, x1, y = 16.0, w - 16.0, h * 0.55
        mm2x = lambda mm: x0 + (x1 - x0) * mm / POS_END
        p.setPen(QPen(QColor("#2f3b49")))
        p.setBrush(QColor("#202a35"))
        p.drawRoundedRect(QRectF(x0, y - 13, x1 - x0, 26), 13, 13)
        p.setPen(QPen(QColor(C["line"])))
        step = 15.0
        off = (self.offset * 0.29) % step
        xx = x0 + off
        while xx < x1:
            p.drawLine(QPointF(xx, y - 11), QPointF(xx, y + 11))
            xx += step
        f = QFont()
        f.setPointSize(8)
        p.setFont(f)
        for i, (mm, name) in enumerate(self.STATIONS):
            x = mm2x(mm)
            cam = name.startswith("Cam")
            up = 14 if cam else 0
            p.setPen(QPen(QColor(C["info"] if cam else "#56677a"), 2))
            p.drawLine(QPointF(x, y - 28 - up), QPointF(x, y - 15))
            p.setPen(QColor("#7dd3fc" if cam else C["muted"]))
            p.drawText(QRectF(x - 40, y - 46 - up, 80, 16), Qt.AlignmentFlag.AlignCenter, name)
            p.setPen(QColor("#56677a"))
            p.drawText(QRectF(x - 30, y + 16 + (8 if cam else 0), 60, 14), Qt.AlignmentFlag.AlignCenter, str(mm))
        for c in self.cans:
            col = QColor(self.COLORS.get(c.state, C["muted"]))
            dy = min(40.0, (c.pos_mm - POS_REJECT) * 0.6) if c.rejected else 0.0
            p.setPen(QPen(QColor(C["bg"]), 2))
            p.setBrush(QBrush(col))
            p.drawEllipse(QPointF(mm2x(c.pos_mm), y + dy), 7, 7)


class BarChart(QWidget):
    """Stacked bars: OK (green) + NG (red) per label."""

    def __init__(self):
        super().__init__()
        self.labels: list[str] = []
        self.ok: list[int] = []
        self.ng: list[int] = []

    def set_data(self, labels: list[str], ok: list[int], ng: list[int]) -> None:
        self.labels, self.ok, self.ng = labels, ok, ng
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        L, B, T = 44, 22, 8
        if not self.labels:
            p.setPen(QColor(C["muted"]))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "Chưa có dữ liệu trong khoảng đã chọn")
            return
        top = max(max(o + n for o, n in zip(self.ok, self.ng)), 1)
        mag = 10 ** max(len(str(top)) - 1, 0)
        top = ((top + mag - 1) // mag) * mag
        f = QFont()
        f.setPointSize(8)
        p.setFont(f)
        for i in range(5):
            v = top * i / 4
            yy = h - B - (h - B - T) * i / 4
            p.setPen(QColor(C["line"]))
            p.drawLine(QPointF(L, yy), QPointF(w, yy))
            p.setPen(QColor(C["muted"]))
            p.drawText(QRectF(0, yy - 8, L - 6, 16), Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                       f"{v / 1000:.1f}k" if top >= 1000 else f"{v:.0f}")
        bw = (w - L - 4) / len(self.labels)
        for i, (lb, o, n) in enumerate(zip(self.labels, self.ok, self.ng)):
            x = L + i * bw + bw * 0.18
            bwi = bw * 0.64
            ho = (h - B - T) * o / top
            hn = (h - B - T) * n / top
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QColor(C["ok"]))
            p.drawRect(QRectF(x, h - B - ho, bwi, ho))
            p.setBrush(QColor(C["err"]))
            p.drawRect(QRectF(x, h - B - ho - hn, bwi, hn))
            p.setPen(QColor(C["muted"]))
            p.drawText(QRectF(x - 10, h - B + 4, bwi + 20, 16), Qt.AlignmentFlag.AlignCenter, lb)
