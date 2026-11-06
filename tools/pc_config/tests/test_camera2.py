"""
End-to-end camera 2 tests with REAL DataMatrix images (generated with zxing-cpp),
placed in a 1280x960 frame with rotation and noise. Skipped if zxing-cpp is missing.
"""
import unittest
from datetime import date

import numpy as np

try:
    import zxingcpp
    from PIL import Image
except ImportError:  # pragma: no cover
    zxingcpp = None

from pmems_config.vision_link import CodeQ

GTIN = "08935049501237"


def make_frame(hri=None, gs1=True, rotate=17, dark=0, light=255, damage=0.0, seed=1):
    """Gray 1280x960 frame: can lid background, white label, DataMatrix in it."""
    rng = np.random.default_rng(seed)
    frame = Image.new("L", (1280, 960), 150)
    if hri is not None:
        b = zxingcpp.create_barcode(hri, zxingcpp.BarcodeFormat.DataMatrix, gs1=gs1)
        code = np.array(zxingcpp.write_barcode_to_image(b, scale=8))
        code = np.where(code < 128, dark, light).astype(np.uint8)
        if damage:
            h, w = code.shape
            m = int(h * damage / 2)
            code[h // 2 - m:h // 2 + m, 4:w - 4] = light          # wipe a band (keep the finder L)
        label = Image.new("L", (code.shape[1] + 120, code.shape[0] + 60), light)
        label.paste(Image.fromarray(code), (90, 30))
        label = label.rotate(rotate, expand=True, fillcolor=150)
        frame.paste(label, (520, 300))
    a = np.array(frame).astype(np.int16) + rng.normal(0, 5, (960, 1280)).astype(np.int16)
    return np.clip(a, 0, 255).astype(np.uint8)


@unittest.skipIf(zxingcpp is None, "zxing-cpp not installed")
class Camera2Test(unittest.TestCase):
    def setUp(self):
        from pmems_config.camera2 import Camera2Station, TraceLog
        from pmems_config.code_check import CodeChecker, CodeRules, DuplicateStore
        from pmems_config.code_reader import CodeReader

        self.trace = TraceLog()
        self.checker = CodeChecker(CodeRules(gtin=GTIN), DuplicateStore())
        self.station = Camera2Station(CodeReader(), self.checker, self.trace, recipe_id=1)

    def run_frame(self, can_id, frame):
        frame_bytes, v = self.station.process(can_id, frame)
        self.assertEqual(frame_bytes[2], 0x21)
        self.assertEqual(frame_bytes[7], int(v.quality))     # QUALITY_FLAG travels to the MCU
        return v

    def test_ok_then_duplicate(self):
        hri = f"(01){GTIN}(21)00AB12CD(17)301231(10)L2611"
        v = self.run_frame(1, make_frame(hri))
        self.assertEqual(v.quality, CodeQ.OK, v.reason)
        self.assertEqual(v.fields["21"], "00AB12CD")
        self.assertEqual(v.hri, hri)
        self.assertEqual(v.grade_letter, "A")
        v2 = self.run_frame(2, make_frame(hri, rotate=-8, seed=2))   # same serial on another can
        self.assertEqual(v2.quality, CodeQ.DUPLICATE)
        self.assertEqual(len(self.trace.recent()), 2)

    def test_wrong_gtin_is_invalid(self):
        v = self.run_frame(3, make_frame("(01)04006381333931(21)ZZ01(17)301231"))
        self.assertEqual(v.quality, CodeQ.INVALID)
        self.assertIn("GTIN", v.reason)

    def test_expired_is_invalid(self):
        frame = make_frame(f"(01){GTIN}(21)EXP001(17)240101")
        v = self.checker.check(self.station.reader.read(frame), today=date(2026, 11, 6))
        self.assertEqual(v.quality, CodeQ.INVALID)
        self.assertIn("hết hạn", v.reason)

    def test_plain_datamatrix_is_invalid(self):
        v = self.run_frame(4, make_frame(f"01{GTIN}21PLAIN01", gs1=False))
        self.assertEqual(v.quality, CodeQ.INVALID)

    def test_no_label_is_no_code(self):
        self.assertEqual(self.run_frame(5, make_frame(None)).quality, CodeQ.NO_CODE)

    def test_heavy_damage_is_rejected(self):
        v = self.run_frame(6, make_frame(f"(01){GTIN}(21)DMG001(17)301231", damage=0.6))
        self.assertIn(v.quality, (CodeQ.UNREADABLE, CodeQ.NO_CODE))

    def test_faded_print_is_low_grade(self):
        v = self.run_frame(7, make_frame(f"(01){GTIN}(21)FADE01(17)301231", dark=110, light=185))
        self.assertEqual(v.quality, CodeQ.LOW_GRADE, v.reason)
        self.assertIn(v.grade_letter, ("D", "F"))

    def test_speed_within_mcu_window(self):
        frame = make_frame(f"(01){GTIN}(21)SPD001(17)301231")
        _, v = self.station.process(8, frame)
        res = self.station.reader.read(frame)
        self.assertLess(res.elapsed_ms, 150.0)


if __name__ == "__main__":
    unittest.main()
