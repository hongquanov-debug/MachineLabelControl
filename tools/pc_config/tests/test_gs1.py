import unittest
from datetime import date

from pmems_config import gs1

GS = gs1.GS


class GS1Test(unittest.TestCase):
    def test_check_digit(self):
        self.assertTrue(gs1.gtin_check_digit_ok("08935049501237"))
        self.assertFalse(gs1.gtin_check_digit_ok("08935049501235"))
        self.assertTrue(gs1.gtin_check_digit_ok("4006381333931"))   # EAN-13

    def test_parse_variable_and_fixed(self):
        raw = "0108935049501237" + "2100AB12CD" + GS + "17271231" + "10L2611"
        f = gs1.parse(raw)
        self.assertEqual(f, {"01": "08935049501237", "21": "00AB12CD", "17": "271231", "10": "L2611"})
        self.assertEqual(gs1.to_hri(f), "(01)08935049501237(21)00AB12CD(17)271231(10)L2611")

    def test_parse_errors(self):
        with self.assertRaises(gs1.GS1Error):
            gs1.parse("0108935049501235")                 # bad check digit
        with self.assertRaises(gs1.GS1Error):
            gs1.parse("0108935049501237" + "17271332")    # month 13
        with self.assertRaises(gs1.GS1Error):
            gs1.parse("99ABC")                            # unknown AI
        with self.assertRaises(gs1.GS1Error):
            gs1.parse("21" + "X" * 21)                    # serial too long

    def test_date_day_zero_is_end_of_month(self):
        self.assertEqual(gs1.parse_date("270200"), date(2027, 2, 28))


if __name__ == "__main__":
    unittest.main()
