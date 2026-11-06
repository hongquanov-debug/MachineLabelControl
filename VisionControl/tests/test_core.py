"""HistoryDB, EventDetector, CanTracker (pure Python, no Qt)."""
import unittest
from datetime import datetime, timedelta

from visioncontrol.core import can_tracker as ct
from visioncontrol.core.events import EventDetector
from visioncontrol.core.history import HistoryDB
from visioncontrol.core.models import CAT_ALARM, LEVEL_ERR, LogEntry, VisionRecord
from visioncontrol.sim.sim_mcu import SimMcu


class HistoryTest(unittest.TestCase):
    def test_totals_hourly_reasons_alarms(self):
        h = HistoryDB()
        t0 = datetime(2026, 1, 2, 8, 0)
        h.add_production(t0 + timedelta(minutes=1), 1, 100, 95, 5, 94, 1, 60)
        h.add_production(t0 + timedelta(minutes=70), 1, 50, 50, 0, 50, 0, 60)
        h.add_vision(VisionRecord(camera=2, can_id=1, quality=0x11, quality_name="UNREADABLE", ok=False,
                                  elapsed_ms=40, ts=t0 + timedelta(minutes=2)))
        h.add_event(LogEntry(LEVEL_ERR, CAT_ALARM, "Alarm", "ESTOP", code="ALM_01", ts=t0 + timedelta(minutes=3)))
        start, end = t0, t0 + timedelta(hours=3)
        t = h.totals(start, end)
        self.assertEqual((t["fed"], t["ok"], t["ng"], t["run_s"]), (150, 145, 5, 120))
        rows = h.hourly(start, end)
        self.assertEqual([r[1:] for r in rows], [(100, 95, 5), (50, 50, 0)])
        self.assertEqual(h.ng_reasons(start, end)[0][1], 1)
        self.assertEqual(h.alarm_counts(start, end)[0][0], "ALM_01")
        self.assertEqual(len(h.events()), 1)


class EventTest(unittest.TestCase):
    def test_state_and_alarm_edges(self):
        mcu = SimMcu()
        ev = EventDetector()
        first = ev.update(mcu.read_status())
        self.assertTrue(any(e.source == "FSM" for e in first))
        mcu.alarms |= 1 << 5
        raised = ev.update(mcu.read_status())
        self.assertEqual([e.code for e in raised if e.category == CAT_ALARM], ["ALM_05"])
        mcu.alarms = 0
        cleared = ev.update(mcu.read_status())
        self.assertTrue(cleared and cleared[0].message.startswith("Đã xóa"))
        self.assertEqual(ev.update(mcu.read_status()), [])


class TrackerTest(unittest.TestCase):
    def test_life_of_a_can(self):
        t = ct.CanTracker()
        t.on_trigger(1, 7)
        t.on_result(1, 7, True)
        self.assertEqual(t.get(7).state, ct.ST_ANGLE_OK)
        t.advance((ct.POS_LABEL - ct.POS_CAM1) / ct.CanTracker.speed_mm_s(120) + 0.01, 120)
        self.assertEqual(t.get(7).state, ct.ST_LABELED)
        t.on_trigger(2, 7)
        self.assertEqual(t.get(7).pos_mm, ct.POS_CAM2)
        t.on_result(2, 7, False)
        t.advance(10, 120)
        self.assertIsNone(t.get(7))           # rejected then dropped

    def test_unknown_can_at_cam2(self):
        t = ct.CanTracker()
        t.on_trigger(2, 99)
        self.assertEqual(t.get(99).pos_mm, ct.POS_CAM2)


if __name__ == "__main__":
    unittest.main()
