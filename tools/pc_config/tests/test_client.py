"""Client tests against an in-memory fake that mimics the firmware register semantics."""
import unittest

from pmems_config import MachineCmd, PmemsClient, PmemsError, Recipe, RecipeStatus
from pmems_config.registers import RCP_CMD, RCP_DATA_COUNT, RCP_FIRST, RCP_STATUS, scale_from_diameter


class _Resp:
    def __init__(self, registers=None, error=False):
        self.registers = registers or []
        self._error = error

    def isError(self):
        return self._error


class FakeController:
    """Behaves like src/app/modbus_regs.c for the registers the client uses."""

    def __init__(self):
        self.holding = [0] * 50
        self.holding[1] = 1                   # active recipe
        self.running = False
        self.jog = False
        self.store = {i: Recipe(recipe_id=i, name=f"R{i}").to_regs() for i in range(1, 17)}
        self.buf = list(self.store[1])
        self.writes = []                      # (address, values) per request

    # ---- pymodbus-like API ----
    def read_holding_registers(self, address, count=1):
        out = []
        for a in range(address, address + count):
            if RCP_FIRST <= a < RCP_FIRST + RCP_DATA_COUNT:
                out.append(self.buf[a - RCP_FIRST])
            else:
                out.append(self.holding[a])
        return _Resp(out)

    def read_input_registers(self, address, count=1):
        regs = [0] * 29
        regs[0] = 4 if self.running else 3
        regs[22] = 0 if self.running else 3
        return _Resp(regs[address:address + count])

    def write_register(self, address, value):
        return self.write_registers(address, [value])

    def write_registers(self, address, values):
        self.writes.append((address, list(values)))
        for i, v in enumerate(values):        # ascending, like the firmware
            a = address + i
            if a == 1 and self.running:
                return _Resp(error=True)      # MB_EX_DEVICE_FAILURE
            if a == 11 and (not self.jog or v & ~0x7FFF):
                return _Resp(error=True)      # 40012 outside MANUAL_JOG
            if a == RCP_FIRST + 13 or a == RCP_STATUS:
                continue                       # read-only
            if RCP_FIRST <= a < RCP_FIRST + RCP_DATA_COUNT:
                self.buf[a - RCP_FIRST] = v
            elif a == RCP_CMD:
                self.holding[RCP_STATUS] = self._exec(v)
            else:
                self.holding[a] = v
        return _Resp()

    def _exec(self, cmd):
        rid = self.buf[0]
        if not 1 <= rid <= 16:
            return RecipeStatus.ERR_ID
        if cmd in (1, 4):
            self.buf = list(self.store[rid])
            return RecipeStatus.OK
        if cmd in (2, 3):
            if (cmd == 3 or rid == self.holding[1]) and self.running:
                return RecipeStatus.ERR_STATE
            r = Recipe.from_regs(self.buf)
            if r.validate():
                return RecipeStatus.ERR_RANGE
            self.buf[13] = scale_from_diameter(r.diameter_mm)
            self.store[rid] = list(self.buf)
            if cmd == 3:
                self.holding[1] = rid
            return RecipeStatus.OK
        return RecipeStatus.ERR_CMD


class ClientTest(unittest.TestCase):
    def setUp(self):
        self.fake = FakeController()
        self.plc = PmemsClient(transport=self.fake)

    def test_read_modify_save_activate(self):
        r = self.plc.read_recipe(5)
        self.assertEqual(r.name, "R5")
        r.diameter_mm = 58.0
        r.name = "CAN_SLEEK"
        saved = self.plc.save_recipe(r, activate=True)
        self.assertEqual(saved.m2_scale_x1000, 611)
        self.assertEqual(self.plc.active_recipe_id(), 5)
        self.assertEqual(self.plc.read_recipe(5).name, "CAN_SLEEK")
        # whole buffer + command went out in ONE request, command last
        saves = [w for w in self.fake.writes if len(w[1]) > 1]
        self.assertEqual(len(saves), 1)
        addr, values = saves[0]
        self.assertEqual(addr, RCP_FIRST)
        self.assertEqual(len(values), RCP_DATA_COUNT + 1)
        self.assertEqual(values[-1], 3)

    def test_local_validation_blocks_bad_values(self):
        r = self.plc.read_recipe(2)
        r.diameter_mm = 10.0
        n = len(self.fake.writes)
        with self.assertRaises(PmemsError) as ctx:
            self.plc.save_recipe(r)
        self.assertEqual(ctx.exception.status, RecipeStatus.ERR_RANGE)
        self.assertEqual(len(self.fake.writes), n)   # nothing sent

    def test_running_machine_rejects_active_change(self):
        self.fake.running = True
        r = self.plc.read_recipe(1)                  # active recipe
        with self.assertRaises(PmemsError) as ctx:
            self.plc.save_recipe(r)
        self.assertEqual(ctx.exception.status, RecipeStatus.ERR_STATE)
        self.plc.save_recipe(self.plc.read_recipe(9))  # other recipes: allowed
        with self.assertRaises(PmemsError):
            self.plc.select_recipe(9)

    def test_status_and_command(self):
        self.assertEqual(self.plc.read_status().state_name, "READY")
        self.plc.send_command(MachineCmd.START_HOMING)
        self.assertEqual(self.fake.holding[0], 5)

    def test_manual_outputs_only_in_jog(self):
        with self.assertRaises(PmemsError):          # READY: refused
            self.plc.set_output("P4", True)
        self.fake.jog = True
        self.plc.set_output("P4", True)
        self.plc.set_output("P3", True)
        self.assertEqual(self.fake.holding[11], 0b1100)
        self.assertTrue(self.plc.read_manual_outputs()["P4"])
        self.plc.set_output("P4", False)
        self.assertEqual(self.fake.holding[11], 0b0100)
        with self.assertRaises(ValueError):
            self.plc.set_output("P9", True)

    def test_argument_checks(self):
        with self.assertRaises(ValueError):
            self.plc.read_recipe(0)
        with self.assertRaises(ValueError):
            self.plc.set_theta_offset(25.0)


if __name__ == "__main__":
    unittest.main()
