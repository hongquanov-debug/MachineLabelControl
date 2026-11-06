"""
Modbus TCP client for PMeMS Controller 1 (W5500, port 502, Spec §7.3).

Typical use on the Vision PC (configuration runs on Ethernet, the angle
results keep using RS485_3 - the two links are independent):

    with PmemsClient("192.168.1.200") as plc:
        r = plc.read_recipe(1)
        r.diameter_mm = 66.0
        plc.save_recipe(r, activate=True)
        print(plc.read_status().state_name)
"""
from __future__ import annotations

from typing import Any, Optional

from .registers import (
    HR_COMMAND,
    HR_RECIPE_SELECT,
    HR_SPEED,
    HR_THETA_OFFSET,
    IR_COUNT,
    RCP_CMD,
    RCP_DATA_COUNT,
    RCP_FIRST,
    RCP_STATUS,
    RECIPE_STATUS_TEXT,
    MachineCmd,
    MachineStatus,
    Recipe,
    RecipeCmd,
    RecipeStatus,
)


class PmemsError(Exception):
    """Communication error or command rejected by the controller."""

    def __init__(self, message: str, status: Optional[RecipeStatus] = None):
        super().__init__(message)
        self.status = status


class PmemsClient:
    """
    Thin, synchronous client. ``transport`` may be injected for tests: any
    object with pymodbus-like ``read_holding_registers / read_input_registers /
    write_register / write_registers`` methods.
    """

    def __init__(self, host: str = "192.168.1.200", port: int = 502,
                 timeout: float = 2.0, transport: Any = None):
        if transport is None:
            from pymodbus.client import ModbusTcpClient  # imported lazily

            transport = ModbusTcpClient(host, port=port, timeout=timeout)
        self._t = transport

    # ---------------- connection ----------------
    def connect(self) -> None:
        connect = getattr(self._t, "connect", None)
        if connect is not None and connect() is False:
            raise PmemsError("Khong ket noi duoc toi bo dieu khien (kiem tra IP / cap mang)")

    def close(self) -> None:
        close = getattr(self._t, "close", None)
        if close is not None:
            close()

    def __enter__(self) -> "PmemsClient":
        self.connect()
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    # ---------------- low level ----------------
    # The unit id is not passed: the firmware ignores it, and the keyword name
    # changed between pymodbus releases ("slave" -> "device_id").
    @staticmethod
    def _check(rr: Any, what: str) -> Any:
        if rr is None or (hasattr(rr, "isError") and rr.isError()):
            raise PmemsError(f"Modbus loi khi {what}: {rr}")
        return rr

    def _read_holding(self, addr: int, count: int) -> list[int]:
        return list(self._check(self._t.read_holding_registers(addr, count=count), "doc holding").registers)

    def _read_input(self, addr: int, count: int) -> list[int]:
        return list(self._check(self._t.read_input_registers(addr, count=count), "doc input").registers)

    def _write(self, addr: int, value: int) -> None:
        self._check(self._t.write_register(addr, value & 0xFFFF), "ghi thanh ghi")

    def _write_many(self, addr: int, values: list[int]) -> None:
        self._check(self._t.write_registers(addr, [v & 0xFFFF for v in values]), "ghi nhieu thanh ghi")

    # ---------------- machine ----------------
    def read_status(self) -> MachineStatus:
        return MachineStatus.from_regs(self._read_input(0, IR_COUNT))

    def send_command(self, cmd: MachineCmd) -> None:
        self._write(HR_COMMAND, int(cmd))

    def active_recipe_id(self) -> int:
        return self._read_holding(HR_RECIPE_SELECT, 1)[0]

    def select_recipe(self, recipe_id: int) -> None:
        """Make a stored recipe active (only when the machine is not running)."""
        if not 1 <= recipe_id <= 16:
            raise ValueError("recipe_id phai 1..16")
        try:
            self._write(HR_RECIPE_SELECT, recipe_id)
        except PmemsError as e:
            raise PmemsError("Khong doi duoc recipe (may dang chay?)") from e

    def set_speed(self, cans_per_min: int) -> None:
        if not 60 <= cans_per_min <= 156:
            raise ValueError("toc do 60..156 lon/phut")
        self._write(HR_SPEED, cans_per_min)

    def set_theta_offset(self, deg: float) -> None:
        v = int(round(deg * 100))
        if not -1800 <= v <= 1800:
            raise ValueError("offset tinh chinh -18.0..+18.0 do")
        self._write(HR_THETA_OFFSET, v)

    # ---------------- recipes ----------------
    def _recipe_cmd_status(self) -> RecipeStatus:
        return RecipeStatus(self._read_holding(RCP_STATUS, 1)[0])

    def _raise_if_failed(self, st: RecipeStatus) -> None:
        if st != RecipeStatus.OK:
            raise PmemsError(RECIPE_STATUS_TEXT.get(st, str(st)), st)

    def read_recipe(self, recipe_id: int, defaults: bool = False) -> Recipe:
        """Load recipe ``recipe_id`` (or its factory defaults) via the edit buffer."""
        if not 1 <= recipe_id <= 16:
            raise ValueError("recipe_id phai 1..16")
        cmd = RecipeCmd.LOAD_DEFAULTS if defaults else RecipeCmd.LOAD
        # id and command in one request: 40021 = id ... 40040 = cmd is too long,
        # so write id then command (the command executes synchronously).
        self._write(RCP_FIRST, recipe_id)
        self._write(RCP_CMD, int(cmd))
        self._raise_if_failed(self._recipe_cmd_status())
        return Recipe.from_regs(self._read_holding(RCP_FIRST, RCP_DATA_COUNT))

    def save_recipe(self, recipe: Recipe, activate: bool = False) -> Recipe:
        """
        Validate locally, then write the whole edit buffer + command in ONE FC16
        request (ascending order: data first, command last). Returns the recipe
        as stored by the controller (with the computed M2 scale).
        """
        errors = recipe.validate()
        if errors:
            raise PmemsError("; ".join(errors), RecipeStatus.ERR_RANGE)
        cmd = RecipeCmd.SAVE_ACTIVATE if activate else RecipeCmd.SAVE
        self._write_many(RCP_FIRST, recipe.to_regs() + [int(cmd)])
        self._raise_if_failed(self._recipe_cmd_status())
        return Recipe.from_regs(self._read_holding(RCP_FIRST, RCP_DATA_COUNT))

    def list_recipes(self) -> list[Recipe]:
        return [self.read_recipe(i) for i in range(1, 17)]
