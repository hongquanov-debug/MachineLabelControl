"""
AppController: the only object the UI talks to.

    UI  --methods-->  AppController  --> PlcWorker (Modbus TCP)      --> STM32
                                     --> SerialLink / SimLink (RS485) --> STM32
                                     --> CameraPipeline x2 (threads)
    UI  <--signals--  AppController  (always delivered in the GUI thread)

Signals are the single source of truth for the pages; pages never call
workers, Modbus or the camera code directly (see UI_SPEC.md).
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

import numpy as np
from PyQt6.QtCore import QObject, QTimer, pyqtSignal
from PyQt6.QtGui import QImage

from pmems_config.registers import MachineCmd, MachineStatus, Recipe

from . import settings as settings_mod
from .core.can_tracker import CanTracker
from .core.events import EventDetector
from .core.history import HistoryDB
from .core.models import CAT_COMM, CAT_VISION, LEVEL_ERR, LEVEL_INFO, LEVEL_WARN, LogEntry, VisionRecord
from .vision import make_angle_detector, make_code_reader, make_source
from .workers.camera_pipeline import CameraPipeline
from .workers.plc_worker import PlcWorker
from .workers.vision_link import SerialLink, SimLink

LOG_MEMORY = 2000


@dataclass
class CameraStats:
    ok: int = 0
    ng: int = 0
    late: int = 0
    duplicate: int = 0
    sum_ms: float = 0.0
    n: int = 0

    @property
    def avg_ms(self) -> float:
        return self.sum_ms / self.n if self.n else 0.0


class AppController(QObject):
    status_changed = pyqtSignal(object)          # MachineStatus
    plc_connection = pyqtSignal(bool, str)
    link_connection = pyqtSignal(bool, str)
    vision_result = pyqtSignal(object)           # VisionRecord
    log_added = pyqtSignal(object)               # LogEntry
    job_done = pyqtSignal(int, object, str)      # job id, result, error text
    settings_changed = pyqtSignal()
    recipe_changed = pyqtSignal()                # emitted by the Config page after select / activate

    # internal: cross-thread hand-over to the GUI thread
    _sig_trigger = pyqtSignal(int, int)
    _sig_result = pyqtSignal(object)
    _sig_link = pyqtSignal(bool, str)

    def __init__(self, cfg: settings_mod.Settings, config_path: str, sim: bool = False):
        super().__init__()
        self.cfg, self.config_path, self.sim = cfg, config_path, sim
        self.data_dir = Path(cfg.data_dir)
        self.history = HistoryDB(self.data_dir / "history.db")
        self.events = EventDetector()
        self.tracker = CanTracker()
        self.status: Optional[MachineStatus] = None
        self.active_recipe = 0                       # set by the header (40002)
        self.belt_mm = 0.0                           # belt travel, for the conveyor animation
        self.plc_ok = False
        self.link_ok = False
        self.log: deque[LogEntry] = deque(maxlen=LOG_MEMORY)
        self.stats = {1: CameraStats(), 2: CameraStats()}
        self.last_record: dict[int, Optional[VisionRecord]] = {1: None, 2: None}
        self.last_ng: dict[int, Optional[VisionRecord]] = {1: None, 2: None}
        self.recent: deque[dict[str, Any]] = deque(maxlen=30)   # per can: id, theta, cam1, cam2

        if sim:
            for c in (cfg.camera1, cfg.camera2):
                c.source = "sim"
            from .sim.sim_mcu import SimMcu
            self.mcu = SimMcu()
            client: Any = self.mcu
            self.link: Any = SimLink(self.mcu)
        else:
            from pmems_config.client import PmemsClient
            self.mcu = None
            client = PmemsClient(cfg.plc.host, cfg.plc.port, cfg.plc.timeout_s)
            self.link = SerialLink(cfg.serial.port, cfg.serial.baud)

        self.plc = PlcWorker(client, cfg.plc.poll_ms)
        self.plc.status.connect(self._on_status)
        self.plc.connection.connect(self._on_plc_connection)
        self.plc.job_done.connect(self.job_done)

        self.pipes = {
            1: CameraPipeline(1, make_source(1, cfg.camera1), make_angle_detector(cfg.camera1), self.link,
                              lambda: self._params(self.cfg.camera1), cfg.camera1.deadline_ms, self._sig_result.emit),
            2: CameraPipeline(2, make_source(2, cfg.camera2), make_code_reader(cfg.camera2), self.link,
                              lambda: self._params(self.cfg.camera2), cfg.camera2.deadline_ms, self._sig_result.emit),
        }
        self.link.on_trigger = self._on_trigger_thread
        self.link.on_status = lambda ok, msg: self._sig_link.emit(ok, msg)
        self._sig_trigger.connect(self.tracker.on_trigger)
        self._sig_result.connect(self._on_result)
        self._sig_link.connect(self._on_link)

        self._prev_counts: Optional[tuple[int, ...]] = None
        self._run_s = 0.0
        self._tick = QTimer(self)
        self._tick.timeout.connect(self._advance)
        self._sample = QTimer(self)
        self._sample.timeout.connect(self._production_sample)

    # ================================================================ lifecycle
    def start(self) -> None:
        if self.mcu:
            self.mcu.start()
        self.link.start()
        for p in self.pipes.values():
            p.start()
            if p.error:
                self._log(LEVEL_ERR, CAT_VISION, f"Cam{p.camera}", p.error)
        self.plc.start()
        self._tick.start(100)
        self._sample.start(60_000)
        self._log(LEVEL_INFO, CAT_COMM, "App", "Khởi động" + (" (chế độ giả lập)" if self.sim else ""))

    def shutdown(self) -> None:
        self._production_sample()
        self.plc.stop()
        for p in self.pipes.values():
            p.stop()
        self.link.stop()
        if self.mcu:
            self.mcu.stop()

    def save_settings(self) -> None:
        settings_mod.save(self.cfg, self.config_path)
        self.settings_changed.emit()

    # ================================================================ commands (non-blocking)
    def command(self, cmd: MachineCmd) -> int:
        return self.plc.submit("send_command", cmd)

    def set_speed(self, cans_per_min: int) -> int:
        return self.plc.submit("set_speed", cans_per_min)

    def set_theta_offset(self, deg: float) -> int:
        return self.plc.submit("set_theta_offset", deg)

    def jog(self, axis: int, direction: int) -> int:
        return self.plc.submit("jog", axis, direction)

    def set_output(self, name: str, on: bool) -> int:
        return self.plc.submit("set_output", name, on)

    def read_recipe(self, recipe_id: int) -> int:
        return self.plc.submit("read_recipe", recipe_id)

    def save_recipe(self, recipe: Recipe, activate: bool) -> int:
        return self.plc.submit("save_recipe", recipe, activate)

    def select_recipe(self, recipe_id: int) -> int:
        return self.plc.submit("select_recipe", recipe_id)

    def active_recipe_id(self) -> int:
        return self.plc.submit("active_recipe_id")

    # simulator only (Monitor > Safety tiles)
    def sim_set_safety(self, key: str, value: bool) -> None:
        if self.mcu:
            self.mcu.sim_set_safety(key, value)

    # ================================================================ internals
    def _params(self, cam: settings_mod.CameraSettings) -> dict[str, Any]:
        p = dict(cam.params)
        if cam.roi:
            p["roi"] = list(cam.roi)
        return p

    def _log(self, level: str, cat: str, src: str, msg: str, code: str = "") -> None:
        e = LogEntry(level, cat, src, msg, code)
        self.log.appendleft(e)
        self.history.add_event(e)
        self.log_added.emit(e)

    def _on_trigger_thread(self, camera: int, can_id: int, t_ms: float) -> None:
        self.pipes[camera].trigger(can_id, t_ms)      # link thread: enqueue only
        self._sig_trigger.emit(camera, can_id)

    def _on_plc_connection(self, ok: bool, msg: str) -> None:
        if ok != self.plc_ok:
            self._log(LEVEL_INFO if ok else LEVEL_ERR, CAT_COMM, "Modbus",
                      f"{'Kết nối' if ok else 'Mất kết nối'} {self.cfg.plc.host}:{self.cfg.plc.port}"
                      + ("" if ok else f" – {msg}"))
        self.plc_ok = ok
        self.plc_connection.emit(ok, msg)

    def _on_link(self, ok: bool, msg: str) -> None:
        if ok != self.link_ok:
            self._log(LEVEL_INFO if ok else LEVEL_ERR, CAT_COMM, "RS485", ("Mở " if ok else "Lỗi ") + msg)
        self.link_ok = ok
        self.link_connection.emit(ok, msg)

    def _on_status(self, st: MachineStatus) -> None:
        if self.status is None or st.state != self.status.state:
            if st.state_name in ("ESTOP", "HOMING"):
                self.tracker.clear()
        self.status = st
        for e in self.events.update(st):
            self.log.appendleft(e)
            self.history.add_event(e)
            self.log_added.emit(e)
        self.status_changed.emit(st)

    def _advance(self) -> None:
        st = self.status
        running = st is not None and st.state_name in ("RUNNING", "PAUSED")
        if running:
            v = st.speed_cans_min or (self.mcu.speed if self.mcu else 0)
            self.tracker.advance(0.1, v)
            self.belt_mm += self.tracker.speed_mm_s(v) * 0.1
        if st is not None and st.state_name == "RUNNING":
            self._run_s += 0.1

    def _on_result(self, rec: VisionRecord) -> None:
        s = self.stats[rec.camera]
        s.ok += rec.ok
        s.ng += not rec.ok
        s.late += rec.late
        s.duplicate += rec.quality_name == "DUPLICATE"
        s.sum_ms += rec.elapsed_ms
        s.n += 1
        self.last_record[rec.camera] = rec
        image_path = ""
        if not rec.ok:
            self.last_ng[rec.camera] = rec
            cam_cfg = self.cfg.camera1 if rec.camera == 1 else self.cfg.camera2
            if cam_cfg.save_ng_images and rec.image is not None and not self.sim:
                image_path = self._save_image(rec)
            self._log(LEVEL_WARN, CAT_VISION, f"Cam{rec.camera}",
                      f"Lon #{rec.can_id}: {rec.quality_name}" + (f" – {rec.reason}" if rec.reason else ""))
        if rec.late:
            self._log(LEVEL_WARN, CAT_VISION, f"Cam{rec.camera}",
                      f"Lon #{rec.can_id}: kết quả trễ {rec.elapsed_ms:.0f} ms (MCU đã loại lon)")
        self.history.add_vision(rec, image_path)
        self.tracker.on_result(rec.camera, rec.can_id, rec.ok)
        row = next((r for r in self.recent if r["id"] == rec.can_id), None)
        if row is None:
            row = {"id": rec.can_id, "theta": None, "cam1": None, "cam2": None}
            self.recent.appendleft(row)
        if rec.camera == 1:
            row["theta"], row["cam1"] = rec.theta_deg, rec.quality_name
        else:
            row["cam2"] = rec.quality_name
        self.vision_result.emit(rec)

    def _save_image(self, rec: VisionRecord) -> str:
        img = np.ascontiguousarray(rec.image)
        d = self.data_dir / "ng" / rec.ts.strftime("%Y%m%d")
        d.mkdir(parents=True, exist_ok=True)
        p = d / f"cam{rec.camera}_{rec.can_id:05d}_{rec.ts:%H%M%S}.png"
        fmt = QImage.Format.Format_Grayscale8 if img.ndim == 2 else QImage.Format.Format_BGR888
        QImage(img.data, img.shape[1], img.shape[0], img.strides[0], fmt).save(str(p))
        return str(p)

    def _production_sample(self) -> None:
        st = self.status
        if st is None:
            return
        cur = (st.fed, st.ok, st.ng, st.code_ok, st.code_ng)
        prev = self._prev_counts or cur
        delta = [c - p if c >= p else c for c, p in zip(cur, prev)]   # counter reset -> take new value
        self._prev_counts = cur
        if any(delta) or self._run_s:
            self.history.add_production(datetime.now(), self.active_recipe, *delta, run_s=self._run_s)
        self._run_s = 0.0
