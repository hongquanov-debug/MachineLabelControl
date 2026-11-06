import unittest

from pmems_config.registers import MachineStatus, Recipe, scale_from_diameter


class RecipeRegsTest(unittest.TestCase):
    def test_layout_matches_firmware_vector(self):
        # Same vector as test_recipe_regs_layout_vector() in test/test_core/test_main.c
        r = Recipe(recipe_id=3, name="CAN_330ML_SLK", diameter_mm=58.0, height_mm=145.0,
                   tab_offset_deg=-12.50, m6_height_mm=59.8)
        regs = r.to_regs()
        self.assertEqual(len(regs), 19)
        self.assertEqual(regs[0], 3)
        self.assertEqual(regs[1], 0x4341)   # "CA"
        self.assertEqual(regs[2], 0x4E5F)   # "N_"
        self.assertEqual(regs[7], 0x4B00)   # "K\0"
        self.assertEqual(regs[8], 0x0000)
        self.assertEqual(regs[9], 580)
        self.assertEqual(regs[11], 0xFB1E)  # -1250
        self.assertEqual(regs[12], 598)
        self.assertEqual(regs[13], 611)

    def test_roundtrip(self):
        r = Recipe(recipe_id=7, name="CAN_250ML", diameter_mm=66.0, height_mm=92.0,
                   tab_offset_deg=35.42, m6_height_mm=6.8, speed_cans_min=150,
                   m4_overrun_pulses=80, press_ms=45, vacuum_detect_ms=60, blow_off_ms=25)
        back = Recipe.from_regs(r.to_regs())
        r.m2_scale_x1000 = 695
        self.assertEqual(back, r)

    def test_scale(self):
        self.assertEqual(scale_from_diameter(66.0), 695)
        self.assertEqual(scale_from_diameter(58.0), 611)
        self.assertEqual(scale_from_diameter(53.0), 558)

    def test_validate(self):
        self.assertEqual(Recipe(recipe_id=1).validate(), [])
        bad = Recipe(recipe_id=17, diameter_mm=20.0, press_ms=500, name="X" * 20)
        self.assertEqual(len(bad.validate()), 4)

    def test_status_decode(self):
        regs = [0] * 23
        regs[0], regs[1] = 4, 7
        regs[3], regs[4] = 0x0001, 0x0002      # fed = 0x00010002
        regs[18], regs[19] = 0, (1 << 7) | (1 << 6)
        regs[22] = 0b101
        st = MachineStatus.from_regs(regs)
        self.assertEqual(st.state_name, "RUNNING")
        self.assertEqual(st.alarm_name, "LABEL_TAPE_EMPTY")
        self.assertEqual(st.fed, 0x00010002)
        self.assertEqual(st.active_alarms, ["VACUUM_FEED_MISS", "LABEL_TAPE_EMPTY"])
        self.assertTrue(st.recipe_change_allowed)
        self.assertFalse(st.eeprom_ok)
        self.assertTrue(st.m6_moving)


if __name__ == "__main__":
    unittest.main()
