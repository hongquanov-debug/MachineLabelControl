"""
Register map of PMeMS Controller 1 - must match the firmware:
  src/app/modbus_regs.h                     (machine / status registers)
  lib/pmems_core/include/recipe_regs.h      (recipe editor 40021..40041)

All addresses below are 0-based protocol addresses (40001 -> 0, 30001 -> 0).
This module is pure Python (no network) so it can be unit tested.
"""
from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum

# ---------------- holding registers (4xxxx) ----------------
HR_COMMAND = 0          # 40001
HR_RECIPE_SELECT = 1    # 40002
HR_SPEED = 2            # 40003
HR_THETA_OFFSET = 3     # 40004 (x100 deg, -1800..1800)
HR_PRESS_MS = 4         # 40005
HR_OVERRUN = 5          # 40006
HR_JOG_AXIS = 9         # 40010 (0=M2 .. 4=M6)
HR_JOG_DIR = 10         # 40011 (-1 / 0 / +1)
HR_MANUAL_DO = 11       # 40012 manual outputs, MANUAL_JOG only

# Bit n of 40012 = DO n (src/app/io_map.h). Only DO 0..14 can be driven by hand.
MANUAL_OUTPUTS = {
    "P1": 0, "P2": 1, "P3": 2, "P4": 3, "P5": 4, "P7": 5, "P8": 6, "M7": 7,
    "VAC1": 8, "VAC2": 9, "VAC3": 10, "VAC4": 11, "VAC5": 12, "VAC6": 13, "VAC_BLOW": 14,
}
MANUAL_DO_MASK = 0x7FFF

RCP_FIRST = 20          # 40021 edit recipe id
RCP_DATA_COUNT = 19     # 40021..40039
RCP_CMD = 39            # 40040
RCP_STATUS = 40         # 40041

# offsets inside the recipe data block
_O_ID, _O_NAME, _O_DIA, _O_H, _O_TAB, _O_M6, _O_SCALE = 0, 1, 9, 10, 11, 12, 13
_O_SPEED, _O_OVERRUN, _O_DWELL, _O_VAC, _O_BLOW = 14, 15, 16, 17, 18

# ---------------- input registers (3xxxx) ----------------
IR_COUNT = 29           # 30001..30029

PU_WHEEL_DIAMETER_MM = 95.0
NAME_LEN = 16


class MachineCmd(IntEnum):
    START = 1           # also RESUME
    STOP = 2
    PAUSE = 3
    RESET_ALARM = 4
    START_HOMING = 5
    JOG_ENTER = 6
    JOG_EXIT = 7


class RecipeCmd(IntEnum):
    LOAD = 1
    SAVE = 2
    SAVE_ACTIVATE = 3
    LOAD_DEFAULTS = 4


class RecipeStatus(IntEnum):
    IDLE = 0
    OK = 1
    ERR_RANGE = 2
    ERR_EEPROM = 3
    ERR_STATE = 4
    ERR_ID = 5
    ERR_CMD = 6


RECIPE_STATUS_TEXT = {
    RecipeStatus.IDLE: "Chua co lenh",
    RecipeStatus.OK: "OK",
    RecipeStatus.ERR_RANGE: "Gia tri ngoai pham vi cho phep",
    RecipeStatus.ERR_EEPROM: "Loi ghi EEPROM (RAM da cap nhat)",
    RecipeStatus.ERR_STATE: "May dang chay - khong doi recipe dang dung",
    RecipeStatus.ERR_ID: "Recipe ID phai tu 1 den 16",
    RecipeStatus.ERR_CMD: "Lenh khong hop le",
}

STATE_NAMES = {
    0: "POWER_ON", 1: "ESTOP", 2: "HOMING", 3: "READY", 4: "RUNNING",
    5: "ALARM", 6: "PAUSED", 7: "MANUAL_JOG", 8: "HOMING_REQUIRED",
}

ALARM_NAMES = {
    0: "NONE",
    1: "ESTOP_ACTIVATED", 2: "SAFETY_DOOR_OPEN", 3: "AIR_PRESSURE_LOW",
    4: "CABINET_OVERTEMP", 5: "VISION_TIMEOUT", 6: "VACUUM_FEED_MISS",
    7: "LABEL_TAPE_EMPTY", 8: "CYL_P5_TIMEOUT", 9: "CODE_CAM_TIMEOUT", 10: "DRIVER_M2_FAULT",
    11: "SERVO_M3_FAULT", 12: "DRIVER_M4_FAULT", 13: "DRIVER_M5_FAULT",
    14: "DRIVER_M6_FAULT", 15: "CYLINDER_TIMEOUT", 16: "HOMING_FAILED",
    17: "VFD_FAULT", 18: "TRACKING_OVERFLOW", 19: "VFD_COMM_LOST",
    20: "LI32_BUS_DISCONNECT", 21: "CODE_NG_REPEATED",
}

CODE_QUALITY_NAMES = {0x00: "OK", 0x10: "NO_CODE", 0x11: "UNREADABLE", 0x12: "INVALID",
                      0x13: "DUPLICATE", 0x14: "LOW_GRADE"}
GRADE_LETTERS = {4: "A", 3: "B", 2: "C", 1: "D", 0: "F"}


def _s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def _u16(v: int) -> int:
    if not -0x8000 <= v <= 0xFFFF:
        raise ValueError(f"value {v} does not fit in 16 bits")
    return v & 0xFFFF


def _x10(v: float) -> int:
    return int(round(v * 10))


def scale_from_diameter(diameter_mm: float) -> int:
    """m2_angle_scale_x1000 = round(D_can / D_PU * 1000) - same as firmware."""
    d10 = _x10(diameter_mm)
    pu10 = _x10(PU_WHEEL_DIAMETER_MM)
    return (d10 * 1000 + pu10 // 2) // pu10


@dataclass
class Recipe:
    """One can recipe in engineering units (Spec §8.1)."""

    recipe_id: int
    name: str = ""
    diameter_mm: float = 66.0
    height_mm: float = 115.2
    tab_offset_deg: float = 0.0       # angle between tab and label centre
    m6_height_mm: float = 30.0        # absolute M6 position
    speed_cans_min: int = 120
    m4_overrun_pulses: int = 50
    press_ms: int = 40                # p5_down_dwell_ms
    vacuum_detect_ms: int = 50
    blow_off_ms: int = 30
    m2_scale_x1000: int = 0           # read-only, computed by the controller

    # Same limits as recipe_check_ranges() in the firmware
    def validate(self) -> list[str]:
        e = []
        if not 1 <= self.recipe_id <= 16:
            e.append("recipe_id phai 1..16")
        if len(self.name.encode("ascii", "replace")) > NAME_LEN - 1:
            e.append("ten toi da 15 ky tu ASCII")
        if not 30.0 <= self.diameter_mm <= 120.0:
            e.append("duong kinh 30..120 mm")
        if not 50.0 <= self.height_mm <= 250.0:
            e.append("chieu cao 50..250 mm")
        if not -180.0 <= self.tab_offset_deg <= 180.0:
            e.append("offset khoen-tem -180..180 do")
        if not 0.0 <= self.m6_height_mm <= 98.0:
            e.append("chieu cao M6 0..98 mm")
        if not 60 <= self.speed_cans_min <= 156:
            e.append("toc do 60..156 lon/phut")
        if not 0 <= self.m4_overrun_pulses <= 200:
            e.append("overrun M4 0..200 xung")
        if not 20 <= self.press_ms <= 100:
            e.append("thoi gian ep 20..100 ms")
        if not 0 <= self.vacuum_detect_ms <= 200:
            e.append("tre kiem tra chan khong 0..200 ms")
        if not 0 <= self.blow_off_ms <= 100:
            e.append("thoi gian thoi 0..100 ms")
        return e

    def to_regs(self) -> list[int]:
        """Recipe -> 19 registers (40021..40039)."""
        raw = self.name.encode("ascii", "replace")[: NAME_LEN - 1].ljust(NAME_LEN, b"\0")
        regs = [0] * RCP_DATA_COUNT
        regs[_O_ID] = self.recipe_id
        for i in range(8):
            regs[_O_NAME + i] = (raw[2 * i] << 8) | raw[2 * i + 1]
        regs[_O_DIA] = _u16(_x10(self.diameter_mm))
        regs[_O_H] = _u16(_x10(self.height_mm))
        regs[_O_TAB] = _u16(int(round(self.tab_offset_deg * 100)))
        regs[_O_M6] = _u16(_x10(self.m6_height_mm))
        regs[_O_SCALE] = scale_from_diameter(self.diameter_mm)
        regs[_O_SPEED] = _u16(self.speed_cans_min)
        regs[_O_OVERRUN] = _u16(self.m4_overrun_pulses)
        regs[_O_DWELL] = _u16(self.press_ms)
        regs[_O_VAC] = _u16(self.vacuum_detect_ms)
        regs[_O_BLOW] = _u16(self.blow_off_ms)
        return regs

    @classmethod
    def from_regs(cls, regs: list[int]) -> "Recipe":
        if len(regs) < RCP_DATA_COUNT:
            raise ValueError("need 19 registers")
        raw = bytearray()
        for i in range(8):
            v = regs[_O_NAME + i]
            raw += bytes([(v >> 8) & 0xFF, v & 0xFF])
        name = raw.split(b"\0", 1)[0].decode("ascii", "replace")
        return cls(
            recipe_id=regs[_O_ID],
            name=name,
            diameter_mm=regs[_O_DIA] / 10.0,
            height_mm=regs[_O_H] / 10.0,
            tab_offset_deg=_s16(regs[_O_TAB]) / 100.0,
            m6_height_mm=regs[_O_M6] / 10.0,
            speed_cans_min=regs[_O_SPEED],
            m4_overrun_pulses=regs[_O_OVERRUN],
            press_ms=regs[_O_DWELL],
            vacuum_detect_ms=regs[_O_VAC],
            blow_off_ms=regs[_O_BLOW],
            m2_scale_x1000=regs[_O_SCALE],
        )


@dataclass
class MachineStatus:
    state: int
    alarm: int
    speed_cans_min: int
    fed: int
    ok: int
    ng: int
    m6_height_mm: float
    air_ok: bool
    di: int
    do: int
    vfd_freq_hz: float
    vfd_current_a: float
    cans_on_belt: int
    alarm_mask: int
    vision_crc_errors: int
    vision_late: int
    recipe_change_allowed: bool
    eeprom_ok: bool
    m6_moving: bool
    safety_bench_mode: bool = False   # firmware built without safety chain (KM1 locked off)
    safety_via_li_ext: bool = False   # PCB v1: safety read through the LI extension
    estop_ok: bool = False            # 30024 bit0: E-Stop released
    door_closed: bool = False         # 30024 bit1
    air_ok_sw: bool = False           # 30024 bit2: air pressure switch
    reset_pressed: bool = False       # 30024 bit3: SAFETY_RESET button
    io_bus_ok: bool = False           # 30024 bit4: LI cable OK
    safety_ext_raw: int = 0           # 30024 high byte: raw LI extension byte
    code_ok: int = 0                  # 30025..26 camera 2 DataMatrix accepted
    code_ng: int = 0                  # 30027..28 camera 2 DataMatrix NG
    code_last_quality: int = 0        # 30029 low byte (CodeQ)
    code_last_grade: int = 0          # 30029 high byte (4=A .. 0=F)

    @property
    def state_name(self) -> str:
        return STATE_NAMES.get(self.state, f"UNKNOWN({self.state})")

    @property
    def alarm_name(self) -> str:
        return ALARM_NAMES.get(self.alarm, f"ALM_{self.alarm:02d}")

    @property
    def active_alarms(self) -> list[str]:
        return [ALARM_NAMES.get(c, f"ALM_{c:02d}") for c in range(1, 32) if self.alarm_mask >> c & 1]

    @classmethod
    def from_regs(cls, r: list[int]) -> "MachineStatus":
        if len(r) < IR_COUNT:
            raise ValueError(f"need {IR_COUNT} input registers")

        def u32(i: int) -> int:  # high word first
            return (r[i] << 16) | r[i + 1]

        return cls(
            state=r[0],
            alarm=r[1],
            speed_cans_min=r[2],
            fed=u32(3),
            ok=u32(5),
            ng=u32(7),
            m6_height_mm=_s16(r[9]) / 10.0,
            air_ok=r[10] > 0,
            di=u32(11),
            do=u32(13),
            vfd_freq_hz=r[15] / 100.0,
            vfd_current_a=r[16] / 10.0,
            cans_on_belt=r[17],
            alarm_mask=u32(18),
            vision_crc_errors=r[20],
            vision_late=r[21],
            recipe_change_allowed=bool(r[22] & 1),
            eeprom_ok=bool(r[22] & 2),
            m6_moving=bool(r[22] & 4),
            safety_bench_mode=bool(r[22] & 8),
            safety_via_li_ext=bool(r[22] & 16),
            estop_ok=bool(r[23] & 1),
            door_closed=bool(r[23] & 2),
            air_ok_sw=bool(r[23] & 4),
            reset_pressed=bool(r[23] & 8),
            io_bus_ok=bool(r[23] & 16),
            safety_ext_raw=r[23] >> 8,
            code_ok=u32(24),
            code_ng=u32(26),
            code_last_quality=r[28] & 0xFF,
            code_last_grade=r[28] >> 8,
        )
