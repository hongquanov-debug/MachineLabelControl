"""Offscreen smoke test: build the window in --sim mode and show every page."""
import json
import os
import tempfile
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
try:
    from PyQt6.QtCore import QEventLoop, QTimer
    HAVE_QT = True
except ImportError:          # pragma: no cover
    HAVE_QT = False


@unittest.skipUnless(HAVE_QT, "PyQt6 not installed")
class UiSmokeTest(unittest.TestCase):
    def test_pages(self):
        from visioncontrol.app import create
        d = tempfile.mkdtemp()
        cfg = os.path.join(d, "settings.json")
        with open(cfg, "w") as f:
            json.dump({"data_dir": os.path.join(d, "data")}, f)
        app, ctrl, win = create(cfg, sim=True, fullscreen=False)
        try:
            def spin(ms):
                loop = QEventLoop()
                QTimer.singleShot(ms, loop.quit)
                loop.exec()
            spin(300)
            for i in range(len(win.pages)):
                win.show_page(i)
                spin(150)
                self.assertFalse(win.grab().isNull())
            self.assertIsNotNone(ctrl.status)
        finally:
            win.close()                       # closeEvent -> ctrl.shutdown()


if __name__ == "__main__":
    unittest.main()
