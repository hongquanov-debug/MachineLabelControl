"""
Simulated STM32 controller for `--sim`.

* Same API as pmems_config.PmemsClient (Modbus TCP side) -> used by PlcWorker.
* Same role as the MCU on RS485_3 (vision link): sends trigger frames 0x10/0x11
  through `on_trigger` and parses result frames written with `write()`.
* Rules copied from the firmware (src/app/tracking.c, machine.c): 80/150 ms
  result windows, only cans with camera-2 OK pass, ALM_05 / ALM_09 / ALM_21,
  E-Stop exit only with the physical SAFETY_RESET.

Runs its own thread; every public method is thread-safe.
"""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Callable, Optional

from pmems_config.registers import MANUAL_DO_MASK, MANUAL_OUTPUTS, MachineCmd, MachineStatus, Recipe

from ..core.vision_protocol import ResultParser

# HMI state codes (firmware machine.h)
POWER_ON, ESTOP, HOMING, READY, RUN, ALARM, PAUSED, JOG, HOME_REQ = range(9)
STOP_ALARMS = {1, 2, 3, 5, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}

DEFAULT_RECIPES = [
    ("CAN_330ML_STD", 66.0, 115.2, 30.0), ("CAN_250ML_LUN", 66.0, 92.0, 6.8),
    ("CAN_330ML_SLK", 58.0, 145.0, 59.8), ("CAN_500ML_CAO", 66.0, 168.0, 82.8),
]


class SimError(Exception):
    pass


@dataclass
class _Can:
    can_id: int
    pos: float = 0.0
    st: str = "new"          # new, wait1, ok1, wait2, pass, ng
    t_trig: float = 0.0


class SimMcu:
    def __init__(self, tick_s: float = 0.002):
        self._lock = threading.RLock()
        self._tick = tick_s
        self.on_trigger: Optional[Callable[[int, int, float], None]] = None   # (camera, can_id, t_ms)
        self._parser = ResultParser()
        self.state = HOME_REQ
        self.speed = 120
        self.offset_x100 = 0
        self.fed = self.ok = self.ng = self.code_ok = self.code_ng = 0
        self.code_last = (0, 0xFF)
        self.alarms = 0
        self.safety = {"estop": True, "door": True, "air": True, "reset": False, "bus": True}
        self.bench = False
        self.manual = 0
        self.jog_axis, self.jog_dir = 0, 0
        self.m6 = 30.0
        self.cans: list[_Can] = []
        self.next_id = 1
        self.belt = 0.0
        self.last_release = 0.0
        self.homing_until = 0.0
        self.consec = {"vis_tmo": 0, "code_tmo": 0, "code_ng": 0}
        self.recipes = [Recipe(recipe_id=i + 1, name=(DEFAULT_RECIPES[i][0] if i < 4 else f"RECIPE_{i + 1:02d}"),
                               diameter_mm=(DEFAULT_RECIPES[i][1] if i < 4 else 66.0),
                               height_mm=(DEFAULT_RECIPES[i][2] if i < 4 else 115.2),
                               m6_height_mm=(DEFAULT_RECIPES[i][3] if i < 4 else 30.0),
                               m2_scale_x1000=round((DEFAULT_RECIPES[i][1] if i < 4 else 66.0) * 1000 / 95))
                        for i in range(16)]
        self.active = 1
        self._running = False
        self._thread: Optional[threading.Thread] = None

    # ================================================================ lifecycle
    def start(self) -> None:
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._loop, name="SimMcu", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=1)

    # PmemsClient compatibility
    def connect(self) -> None:
        self.start()

    def close(self) -> None:
        pass

    # ================================================================ simulation
    def _loop(self) -> None:
        last = time.perf_counter()
        while self._running:
            time.sleep(self._tick)
            now = time.perf_counter()
            with self._lock:
                self._step(now - last, now * 1000.0)
            last = now

    def _raise(self, code: int) -> None:
        self.alarms |= 1 << code

    def _step(self, dt: float, now_ms: float) -> None:
        s = self.safety
        if not self.bench:
            ok_bus = s["bus"]
            estop_ok = s["estop"] and ok_bus
            if not ok_bus:
                self._raise(20)
            if not estop_ok:
                self._raise(1)
                if self.state != ESTOP:
                    self._enter(ESTOP)
            if not (s["door"] and ok_bus):
                self._raise(2)
                if self.state in (RUN, PAUSED, HOMING, JOG):
                    self._enter(ESTOP)
            elif self.state not in (RUN, PAUSED, HOMING, JOG, ESTOP):
                self.alarms &= ~(1 << 2)
            if not (s["air"] and ok_bus):
                self._raise(3)
            if self.state == ESTOP and estop_ok and s["door"] and s["reset"]:
                self.alarms = 0
                self._enter(HOME_REQ)
        if self.state == HOMING and now_ms >= self.homing_until:
            self._enter(READY)
        if self.state in (RUN, PAUSED) and self.alarms & sum(1 << c for c in STOP_ALARMS):
            self._enter(ALARM)

        running = self.state in (RUN, PAUSED)
        v = self.speed * 120.0 / 60.0 if running else 0.0
        self.belt += v * dt
        if self.state == RUN and self.belt - self.last_release >= 120.0:
            self.last_release = self.belt
            self.cans.append(_Can(self.next_id))
            self.next_id = self.next_id % 65535 + 1
            self.fed += 1
        for c in self.cans:
            c.pos += v * dt
            if c.st == "new" and c.pos >= 50:
                c.st, c.t_trig = "wait1", now_ms
                self._trigger(1, c.can_id, now_ms)
            elif c.st == "wait1" and now_ms - c.t_trig > 80:
                c.st = "ng"
                self.consec["vis_tmo"] += 1
                if self.consec["vis_tmo"] >= 3:
                    self._raise(5)
            elif c.st == "ok1" and c.pos >= 1050:
                c.st, c.t_trig = "wait2", now_ms
                self._trigger(2, c.can_id, now_ms)
            elif c.st == "wait2" and now_ms - c.t_trig > 150:
                c.st = "ng"
                self.code_ng += 1
                self.consec["code_tmo"] += 1
                if self.consec["code_tmo"] >= 3:
                    self._raise(9)
        keep = []
        for c in self.cans:
            if c.pos >= 1250 and c.st != "pass":
                self.ng += 1                                   # P8 rejects everything not verified
            elif c.pos >= 2000:
                self.ok += 1
            else:
                keep.append(c)
        self.cans = keep

    def _trigger(self, camera: int, can_id: int, now_ms: float) -> None:
        cb = self.on_trigger
        if cb:
            cb(camera, can_id, now_ms)

    def _enter(self, st: int) -> None:
        if st == ESTOP:
            self.cans.clear()
            self.manual = 0
        if self.state == JOG and st != JOG:
            self.manual = 0
        if st == HOMING:
            self.homing_until = time.perf_counter() * 1000.0 + 3000.0
            self.cans.clear()
        self.state = st

    # ================================================================ RS485 side
    def write(self, data: bytes) -> None:
        """Result frames from the PC (0x20 / 0x21)."""
        with self._lock:
            for r in self._parser.feed(data):
                c = next((x for x in self.cans if x.can_id == r.can_id), None)
                if r.camera == 1 and c and c.st == "wait1":
                    self.consec["vis_tmo"] = 0
                    c.st = "ok1" if r.quality == 0 else "ng"
                elif r.camera == 2 and c and c.st == "wait2":
                    self.consec["code_tmo"] = 0
                    self.code_last = (r.quality, r.grade)
                    if r.quality == 0:
                        c.st = "pass"
                        self.code_ok += 1
                        self.consec["code_ng"] = 0
                    else:
                        c.st = "ng"
                        self.code_ng += 1
                        self.consec["code_ng"] += 1
                        if self.consec["code_ng"] >= 5:
                            self._raise(21)

    # ================================================================ PmemsClient API
    def read_status(self) -> MachineStatus:
        with self._lock:
            return MachineStatus.from_regs(self._regs())

    def _regs(self) -> list[int]:
        r = [0] * 29
        u32 = lambda i, v: (r.__setitem__(i, (v >> 16) & 0xFFFF), r.__setitem__(i + 1, v & 0xFFFF))
        r[0] = self.state
        mask = self.alarms
        r[1] = next((c for c in sorted(STOP_ALARMS) if mask >> c & 1), next((c for c in range(32) if mask >> c & 1), 0))
        r[2] = self.speed if self.state == RUN else 0
        u32(3, self.fed)
        u32(5, self.ok)
        u32(7, self.ng)
        r[9] = int(self.m6 * 10)
        s = self.safety
        r[10] = 50 if s["air"] else 0
        u32(11, self._di())
        u32(13, self._do())
        r[15] = int(self.speed * 2 / 6.24 * 100) if self.state == RUN else 0
        r[16] = 12 if self.state == RUN else 0
        r[17] = len(self.cans)
        u32(18, mask)
        allowed = self.state not in (RUN, PAUSED, HOMING)
        r[22] = (1 if allowed else 0) | 2 | (8 if self.bench else 0) | 16
        ext = (s["estop"] | s["door"] << 1 | s["air"] << 2 | s["reset"] << 3) if s["bus"] else 0xFF
        sf = self._safety_view()
        r[23] = (sf["estop"] | sf["door"] << 1 | sf["air"] << 2 | sf["reset"] << 3 | s["bus"] << 4) | (ext << 8)
        u32(24, self.code_ok)
        u32(26, self.code_ng)
        r[28] = (self.code_last[1] & 0xFF) << 8 | self.code_last[0]
        return r

    def _safety_view(self) -> dict[str, int]:
        s = self.safety
        if self.bench:
            return {"estop": 1, "door": 1, "air": 1, "reset": 0}
        b = s["bus"]
        return {k: int(s[k] and b) for k in ("estop", "door", "air", "reset")}

    def _di(self) -> int:
        m = self.manual
        di = 0b1111 | (1 << 22)                                  # label lanes, S0 queue
        for bit_out, ext_bit, ret_bit in ((0, 4, 5), (1, 6, 7), (2, 8, 9), (3, 10, 11), (6, 14, 15)):
            di |= (1 << ext_bit) if (m >> bit_out & 1) or (bit_out == 0 and self.state != JOG) else (1 << ret_bit)
        di |= (1 << 13) if m >> 4 & 1 else (1 << 12)            # P5 down / up
        if self.state == RUN:
            di |= 0b111111 << 16                                  # vacuum switches
        return di

    def _do(self) -> int:
        d = self.manual & MANUAL_DO_MASK
        if self.state != JOG:
            d |= 1                                                # P1 blocking
        if self.state == RUN:
            d |= (1 << 7) | (0b111111 << 8) | (1 << 23)
        if self.state in (ESTOP, POWER_ON):
            d |= 0b1111 << 16                                     # drivers disabled
        else:
            d |= 1 << 20                                          # servo on
        if self.alarms:
            d |= 1 << 21
        if self.state == PAUSED:
            d |= 1 << 22
        return d

    def send_command(self, cmd: MachineCmd) -> None:
        with self._lock:
            c, st = int(cmd), self.state
            if c == MachineCmd.START and st in (READY, PAUSED) and not self._has_stop_alarm() and self.safety["door"]:
                self._enter(RUN)
            elif c == MachineCmd.STOP and st in (RUN, PAUSED):
                self._enter(READY)
            elif c == MachineCmd.PAUSE and st == RUN:
                self._enter(PAUSED)
            elif c == MachineCmd.RESET_ALARM and st != ESTOP:
                self.alarms = 0
                if st == ALARM:
                    self._enter(READY)
            elif c == MachineCmd.START_HOMING and st in (HOME_REQ, READY, ALARM) and not self._has_stop_alarm():
                self._enter(HOMING)
            elif c == MachineCmd.JOG_ENTER and st == READY:
                self._enter(JOG)
            elif c == MachineCmd.JOG_EXIT and st == JOG:
                self._enter(READY)

    def _has_stop_alarm(self) -> bool:
        return any(self.alarms >> c & 1 for c in STOP_ALARMS)

    def active_recipe_id(self) -> int:
        return self.active

    def select_recipe(self, recipe_id: int) -> None:
        with self._lock:
            if self.state in (RUN, PAUSED, HOMING):
                raise SimError("Không đổi được recipe (máy đang chạy?)")
            self.active = recipe_id
            self.speed = self.recipes[recipe_id - 1].speed_cans_min
            self.m6 = self.recipes[recipe_id - 1].m6_height_mm

    def set_speed(self, cans_per_min: int) -> None:
        with self._lock:
            self.speed = max(60, min(156, int(cans_per_min)))

    def set_theta_offset(self, deg: float) -> None:
        with self._lock:
            self.offset_x100 = int(round(deg * 100))

    def jog(self, axis: int, direction: int) -> None:
        with self._lock:
            self.jog_axis, self.jog_dir = axis, direction
            if self.state == JOG and axis == 4 and direction:
                self.m6 = max(0.0, min(98.0, self.m6 + 0.5 * direction))

    def read_manual_outputs(self) -> dict[str, bool]:
        with self._lock:
            return {n: bool(self.manual >> b & 1) for n, b in MANUAL_OUTPUTS.items()}

    def set_manual_outputs(self, mask: int) -> None:
        with self._lock:
            if self.state != JOG:
                raise SimError("Chỉ điều khiển tay ở chế độ MANUAL_JOG")
            self.manual = mask & MANUAL_DO_MASK

    def set_output(self, name: str, on: bool) -> None:
        with self._lock:
            bit = 1 << MANUAL_OUTPUTS[name]
            self.set_manual_outputs(self.manual | bit if on else self.manual & ~bit)

    def read_recipe(self, recipe_id: int, defaults: bool = False) -> Recipe:
        with self._lock:
            r = self.recipes[recipe_id - 1]
            return Recipe(**{**r.__dict__})

    def save_recipe(self, recipe: Recipe, activate: bool = False) -> Recipe:
        with self._lock:
            errs = recipe.validate()
            if errs:
                raise SimError("; ".join(errs))
            if (activate or recipe.recipe_id == self.active) and self.state in (RUN, PAUSED, HOMING):
                raise SimError("Máy đang chạy - không đổi recipe đang dùng")
            saved = Recipe(**{**recipe.__dict__, "m2_scale_x1000": round(recipe.diameter_mm * 1000 / 95)})
            self.recipes[recipe.recipe_id - 1] = saved
            if activate:
                self.active = recipe.recipe_id
                self.speed = saved.speed_cans_min
                self.m6 = saved.m6_height_mm
            return saved

    # ================================================================ sim-only controls (Monitor > Safety)
    def sim_set_safety(self, key: str, value: bool) -> None:
        with self._lock:
            self.safety[key] = value
