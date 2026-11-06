"""
RS485_3 protocol between the Vision PC and the MCU (docs/VISION_PROTOCOL.md).

Pure Python (no serial port here) so it can be unit tested. Use it with pyserial:

    import serial
    from pmems_config.vision_link import TriggerParser, result_cam1, result_cam2, CodeQ

    port = serial.Serial("COM3", 115200, timeout=0.005)
    parser = TriggerParser()
    while True:
        for trig in parser.feed(port.read(64)):
            if trig.camera == 1:
                theta = measure_tab_angle(trig.can_id)               # your algorithm
                port.write(result_cam1(trig.can_id, theta, ok=True))
            else:
                text, grade = read_datamatrix(trig.can_id)            # your decoder
                q = CodeQ.OK if is_valid(text) else CodeQ.INVALID
                port.write(result_cam2(trig.can_id, q, grade, len(text)))

Deadlines (MCU side): camera 1 result within 80 ms of CAM_TRIG,
camera 2 result within 150 ms of CAM2_TRIG. Late results are ignored and the
can is rejected.
"""
from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import Iterable

CMD_TRIGGER_CAM1 = 0x10
CMD_TRIGGER_CAM2 = 0x11
CMD_RESULT_CAM1 = 0x20
CMD_RESULT_CAM2 = 0x21


class VisionQ(IntEnum):
    """QUALITY_FLAG of a camera 1 (0x20) result - spec section 2.2.B."""
    OK = 0x00
    NO_TAB = 0x01
    DEFECT = 0x02
    WRONG_TYPE = 0x03


class CodeQ(IntEnum):
    """QUALITY_FLAG of a camera 2 (0x21) DataMatrix result."""
    OK = 0x00          # found, decoded, content accepted
    NO_CODE = 0x10     # no label / no DataMatrix in the image
    UNREADABLE = 0x11  # code found, cannot be decoded
    INVALID = 0x12     # decoded, content rejected (format / wrong product)
    DUPLICATE = 0x13   # decoded, serial already seen
    LOW_GRADE = 0x14   # decoded, print quality below the limit


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def _frame12(cmd: int, can_id: int, b5: int, b6: int, quality: int) -> bytes:
    body = bytes([0xAA, 0x55, cmd, can_id & 0xFF, (can_id >> 8) & 0xFF, b5 & 0xFF, b6 & 0xFF, quality & 0xFF])
    crc = crc16_modbus(body)
    return body + bytes([crc & 0xFF, crc >> 8, 0x0D, 0x0A])


def result_cam1(can_id: int, theta_deg: float, ok: bool = True, quality: VisionQ | None = None) -> bytes:
    """Camera 1 result (0x20): compensation angle in degrees (-180..+180)."""
    q = VisionQ.OK if ok and quality is None else (quality if quality is not None else VisionQ.NO_TAB)
    th = int(round(max(-180.0, min(180.0, theta_deg)) * 100)) & 0xFFFF
    return _frame12(CMD_RESULT_CAM1, can_id, th & 0xFF, th >> 8, q)


def result_cam2(can_id: int, quality: CodeQ, grade: int = 0xFF, data_len: int = 0) -> bytes:
    """Camera 2 result (0x21): grade 4=A .. 0=F (0xFF = not measured), data_len = decoded length."""
    return _frame12(CMD_RESULT_CAM2, can_id, grade, min(data_len, 255), quality)


@dataclass
class Trigger:
    camera: int     # 1 = tab angle, 2 = DataMatrix
    can_id: int


class TriggerParser:
    """Byte-stream parser for the MCU trigger frames 55 AA 10|11 ID_L ID_H CRC_L CRC_H 0A."""

    def __init__(self) -> None:
        self._buf = bytearray()
        self.crc_errors = 0

    def feed(self, data: bytes | Iterable[int]) -> list[Trigger]:
        self._buf.extend(data)
        out: list[Trigger] = []
        while True:
            i = self._buf.find(b"\x55\xAA")
            if i < 0:
                del self._buf[:-1]                 # keep a possible half header
                return out
            del self._buf[:i]
            if len(self._buf) < 8:
                return out
            f = bytes(self._buf[:8])
            if f[2] in (CMD_TRIGGER_CAM1, CMD_TRIGGER_CAM2) and f[7] == 0x0A and \
                    crc16_modbus(f[:5]) == f[5] | (f[6] << 8):
                out.append(Trigger(1 if f[2] == CMD_TRIGGER_CAM1 else 2, f[3] | (f[4] << 8)))
                del self._buf[:8]
            else:
                self.crc_errors += 1
                del self._buf[:1]                  # resync on the next 55 AA
