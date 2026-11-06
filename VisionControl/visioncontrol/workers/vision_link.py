"""
RS485_3 link to the MCU (docs/VISION_PROTOCOL.md).

ONE thread owns the serial port: it reads trigger frames (0x10 / 0x11) and
writes the result frames (0x20 / 0x21) queued by the camera pipelines. The bus
is half-duplex, so no other code may write to the port.

`on_trigger(camera, can_id, t_ms)` is called from the link thread; it must only
enqueue (CameraPipeline.trigger does exactly that).
"""
from __future__ import annotations

import queue
import threading
import time
from typing import Callable, Optional

from ..core.vision_protocol import TriggerParser

TriggerCallback = Callable[[int, int, float], None]


class SerialLink:
    def __init__(self, port: str, baud: int = 115200):
        self.port_name, self.baud = port, baud
        self.on_trigger: Optional[TriggerCallback] = None
        self.on_status: Optional[Callable[[bool, str], None]] = None
        self.crc_errors = 0
        self._tx: "queue.Queue[bytes]" = queue.Queue()
        self._running = False
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        self._running = True
        self._thread = threading.Thread(target=self._loop, name="SerialLink", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)

    def write(self, frame: bytes) -> None:
        """Thread-safe: queue a result frame for the single writer."""
        self._tx.put(frame)

    def _status(self, ok: bool, msg: str) -> None:
        if self.on_status:
            self.on_status(ok, msg)

    def _loop(self) -> None:
        import serial  # pyserial, imported here so --sim works without it

        parser = TriggerParser()
        while self._running:
            try:
                with serial.Serial(self.port_name, self.baud, timeout=0.002) as port:
                    self._status(True, f"{self.port_name} {self.baud}")
                    while self._running:
                        data = port.read(64)
                        if data:
                            t = time.perf_counter() * 1000.0
                            for trig in parser.feed(data):
                                if self.on_trigger:
                                    self.on_trigger(trig.camera, trig.can_id, t)
                            self.crc_errors = parser.crc_errors
                        while True:
                            try:
                                port.write(self._tx.get_nowait())
                            except queue.Empty:
                                break
            except Exception as e:  # noqa: BLE001 - port missing / unplugged: retry
                self._status(False, str(e))
                time.sleep(2.0)


class SimLink:
    """Vision link bound to the simulated MCU (same interface as SerialLink)."""

    def __init__(self, mcu):
        self.mcu = mcu
        self.on_trigger: Optional[TriggerCallback] = None
        self.on_status: Optional[Callable[[bool, str], None]] = None
        self.crc_errors = 0

    def start(self) -> None:
        self.mcu.on_trigger = lambda cam, cid, t: self.on_trigger and self.on_trigger(cam, cid, t)
        if self.on_status:
            self.on_status(True, "Giả lập")

    def stop(self) -> None:
        self.mcu.on_trigger = None

    def write(self, frame: bytes) -> None:
        self.mcu.write(frame)
