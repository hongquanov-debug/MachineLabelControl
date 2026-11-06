"""Builds the QApplication, the controller and the main window."""
from __future__ import annotations

import sys

from PyQt6.QtWidgets import QApplication

from . import settings as settings_mod
from .controller import AppController
from .ui.main_window import MainWindow
from .ui.theme import QSS


def create(config_path: str = "config/settings.json", sim: bool = False,
           fullscreen: bool | None = None) -> tuple[QApplication, AppController, MainWindow]:
    app = QApplication.instance() or QApplication(sys.argv)
    app.setStyleSheet(QSS)
    cfg = settings_mod.load(config_path)
    ctrl = AppController(cfg, config_path, sim=sim)
    win = MainWindow(ctrl)
    if fullscreen if fullscreen is not None else cfg.fullscreen:
        win.showFullScreen()
    else:
        win.setFixedSize(1024, 768)
        win.show()
    ctrl.start()
    return app, ctrl, win
