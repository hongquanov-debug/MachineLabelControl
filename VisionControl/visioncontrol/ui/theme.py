"""Colours (same as docs/ui/styles.css) and the application style sheet."""

C = {
    "bg": "#0e1318", "panel": "#171e26", "panel2": "#1e2731", "line": "#2a3542",
    "text": "#e6edf3", "muted": "#8b98a6", "accent": "#3b8bf6",
    "ok": "#22c55e", "warn": "#f5a524", "err": "#ef4444", "info": "#38bdf8", "off": "#3a4553",
}

QSS = f"""
QWidget {{ background: {C['bg']}; color: {C['text']}; font-family: "Segoe UI", "Noto Sans", "DejaVu Sans"; font-size: 13px; }}
QLabel {{ background: transparent; }}
QToolTip {{ background: {C['panel2']}; color: {C['text']}; border: 1px solid {C['line']}; }}

QFrame#header, QFrame#nav, QFrame#footer {{ background: {C['panel']}; border: 0; }}
QFrame#header {{ border-bottom: 1px solid {C['line']}; }}
QFrame#nav {{ border-right: 1px solid {C['line']}; }}
QFrame#footer {{ border-top: 1px solid {C['line']}; }}
QFrame#footer[alarm="true"] {{ background: #3a1416; border-top: 1px solid {C['err']}; }}
QLabel#footerText {{ color: {C['muted']}; }}
QFrame#footer[alarm="true"] QLabel#footerText {{ color: #fecaca; font-weight: 600; }}
QLabel#brand {{ font-size: 16px; font-weight: 700; }}
QLabel#brandSub, QLabel#hdrSmall, QLabel#muted, QLabel#hint {{ color: {C['muted']}; font-size: 10px; }}
QLabel#hdrValue {{ font-size: 14px; font-weight: 600; }}
QLabel#clock {{ font-family: Consolas, "DejaVu Sans Mono"; font-size: 15px; }}
QLabel#benchBanner {{ background: #9a3412; color: white; font-weight: 700; font-size: 12px; padding: 4px; }}

QToolButton#navBtn {{ background: transparent; color: {C['muted']}; border: 0; border-radius: 8px;
    font-size: 12px; padding: 6px; }}
QToolButton#navBtn:hover {{ background: {C['panel2']}; color: {C['text']}; }}
QToolButton#navBtn:checked {{ background: rgba(59,139,246,0.16); color: {C['accent']}; }}

QLabel#statePill {{ border-radius: 17px; padding: 0 14px; font-weight: 700; font-size: 14px;
    background: {C['off']}; color: white; }}
QLabel#statePill[kind="run"] {{ background: rgba(34,197,94,0.18); color: {C['ok']}; border: 1px solid {C['ok']}; }}
QLabel#statePill[kind="ready"] {{ background: rgba(59,139,246,0.18); color: {C['accent']}; border: 1px solid {C['accent']}; }}
QLabel#statePill[kind="warn"] {{ background: rgba(245,165,36,0.18); color: {C['warn']}; border: 1px solid {C['warn']}; }}
QLabel#statePill[kind="err"] {{ background: rgba(239,68,68,0.2); color: {C['err']}; border: 1px solid {C['err']}; }}

QLabel#safetyChip {{ border-radius: 15px; padding: 0 12px; font-size: 12px; font-weight: 700; }}
QLabel#safetyChip[kind="ok"] {{ background: rgba(34,197,94,0.12); color: {C['ok']}; border: 1px solid rgba(34,197,94,0.5); }}
QLabel#safetyChip[kind="err"] {{ background: {C['err']}; color: white; }}
QLabel#safetyChip[kind="bench"] {{ background: #7c2d12; color: #fed7aa; border: 1px solid #ea580c; }}

QFrame#card {{ background: {C['panel']}; border: 1px solid {C['line']}; border-radius: 8px; }}
QLabel#cardTitle {{ color: {C['muted']}; font-size: 12px; font-weight: 600; letter-spacing: 0.5px; }}
QFrame#kpi {{ background: {C['panel']}; border: 1px solid {C['line']}; border-radius: 8px; }}
QLabel#kpiTitle {{ color: {C['muted']}; font-size: 11px; }}
QLabel#kpiValue {{ font-family: Consolas, "DejaVu Sans Mono"; font-size: 24px; font-weight: 600; }}
QLabel#kpiSub {{ color: {C['muted']}; font-size: 11px; }}
QFrame#stat {{ background: {C['panel2']}; border-radius: 6px; }}
QLabel#statValue {{ font-family: Consolas, "DejaVu Sans Mono"; font-size: 15px; font-weight: 600; }}
QLabel#bigValue {{ font-family: Consolas, "DejaVu Sans Mono"; font-size: 38px; font-weight: 700; }}
QLabel#mono {{ font-family: Consolas, "DejaVu Sans Mono"; }}
QLabel#dataBox {{ background: #0b1015; border: 1px solid {C['line']}; border-radius: 6px; padding: 6px 8px;
    font-family: Consolas, "DejaVu Sans Mono"; font-size: 12px; }}
QLabel#helpBox {{ background: rgba(239,68,68,0.12); border: 1px solid {C['err']}; border-radius: 8px;
    padding: 8px 10px; color: #fecaca; }}

QPushButton {{ min-height: 44px; padding: 0 14px; border-radius: 6px; border: 1px solid {C['line']};
    background: {C['panel2']}; font-weight: 600; }}
QPushButton:hover {{ border-color: #3d4b5c; }}
QPushButton:disabled {{ color: #5b6673; background: #161d25; }}
QPushButton[size="sm"] {{ min-height: 34px; font-size: 12px; padding: 0 10px; }}
QPushButton[kind="primary"] {{ background: {C['accent']}; border-color: {C['accent']}; color: white; }}
QPushButton[kind="ok"] {{ background: #15803d; border-color: #16a34a; color: white; }}
QPushButton[kind="warn"] {{ background: #b7791f; border-color: #d69e2e; color: white; }}
QPushButton[kind="err"] {{ background: #b91c1c; border-color: #dc2626; color: white; }}
QPushButton[kind]:disabled {{ background: #1b232c; border-color: {C['line']}; color: #5b6673; }}
QPushButton[big="true"] {{ min-height: 56px; font-size: 15px; }}
QPushButton#seg {{ min-height: 34px; border-radius: 0; font-size: 12px; color: {C['muted']}; }}
QPushButton#seg:checked {{ background: {C['accent']}; color: white; border-color: {C['accent']}; }}

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QDateEdit {{ min-height: 36px; background: {C['panel2']};
    border: 1px solid {C['line']}; border-radius: 6px; padding: 0 8px; font-size: 14px; }}
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {{ subcontrol-origin: border; width: 0px; height: 0px; border: none; }}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {{ border-color: {C['accent']}; }}
QLineEdit:read-only {{ border-style: dashed; background: transparent; }}
QComboBox QAbstractItemView {{ background: {C['panel2']}; selection-background-color: {C['accent']}; }}

QTabWidget::pane {{ border: 0; }}
QTabBar::tab {{ background: transparent; color: {C['muted']}; padding: 9px 16px; font-weight: 600;
    border-bottom: 2px solid transparent; }}
QTabBar::tab:selected {{ color: {C['text']}; border-bottom: 2px solid {C['accent']}; }}

QTableView, QTableWidget, QListWidget {{ background: {C['panel']}; alternate-background-color: #1a222b;
    gridline-color: {C['line']}; border: 1px solid {C['line']}; border-radius: 6px; font-size: 12px; }}
QHeaderView::section {{ background: #212b36; color: {C['muted']}; border: 0; padding: 5px 6px; font-weight: 600; }}
QTableView::item:selected, QListWidget::item:selected {{ background: rgba(59,139,246,0.3); }}
QListWidget::item {{ padding: 6px 4px; border-bottom: 1px solid {C['line']}; }}
QScrollBar:vertical {{ background: transparent; width: 10px; }}
QScrollBar::handle:vertical {{ background: {C['off']}; border-radius: 5px; min-height: 30px; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; }}
QProgressBar {{ background: {C['off']}; border: 0; border-radius: 4px; max-height: 8px; }}
QProgressBar::chunk {{ background: {C['ok']}; border-radius: 4px; }}
QProgressBar[kind="err"]::chunk {{ background: {C['err']}; }}
QMessageBox {{ background: {C['panel']}; }}
"""


def repolish(w) -> None:
    """Re-apply the style sheet after changing a dynamic property (setProperty)."""
    w.style().unpolish(w)
    w.style().polish(w)
    w.update()
