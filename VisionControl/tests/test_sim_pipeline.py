"""End-to-end without Qt: SimMcu -> SimLink -> CameraPipeline (sim camera + sim algorithms) -> result frames -> SimMcu."""
import time
import unittest

from pmems_config.registers import MachineCmd
from visioncontrol.settings import Settings
from visioncontrol.sim.sim_mcu import ESTOP, READY, RUN, SimMcu
from visioncontrol.vision import make_angle_detector, make_code_reader, make_source
from visioncontrol.workers.camera_pipeline import CameraPipeline
from visioncontrol.workers.vision_link import SimLink


def wait(cond, timeout=8.0):
    t = time.monotonic() + timeout
    while time.monotonic() < t:
        if cond():
            return True
        time.sleep(0.02)
    return False


class SimPipelineTest(unittest.TestCase):
    def setUp(self):
        cfg = Settings()
        self.mcu = SimMcu()
        self.link = SimLink(self.mcu)
        self.records = []
        self.pipes = {
            1: CameraPipeline(1, make_source(1, cfg.camera1), make_angle_detector(cfg.camera1), self.link,
                              lambda: dict(cfg.camera1.params), cfg.camera1.deadline_ms, self.records.append),
            2: CameraPipeline(2, make_source(2, cfg.camera2), make_code_reader(cfg.camera2), self.link,
                              lambda: dict(cfg.camera2.params), cfg.camera2.deadline_ms, self.records.append),
        }
        self.link.on_trigger = lambda cam, cid, t: self.pipes[cam].trigger(cid, t)
        for p in self.pipes.values():
            p.start()
        self.link.start()
        self.mcu.start()

    def tearDown(self):
        self.mcu.stop()
        self.link.stop()
        for p in self.pipes.values():
            p.stop()

    def test_run_counts_and_estop(self):
        self.mcu.send_command(MachineCmd.START_HOMING)
        self.assertTrue(wait(lambda: self.mcu.read_status().state == READY))
        self.mcu.send_command(MachineCmd.START)
        self.assertTrue(wait(lambda: self.mcu.read_status().state == RUN, 2))
        self.assertTrue(wait(lambda: self.mcu.read_status().ok >= 3, 20))
        st = self.mcu.read_status()
        self.assertGreater(st.code_ok, 0)
        self.assertTrue(any(r.camera == 1 for r in self.records))
        self.assertTrue(any(r.camera == 2 for r in self.records))
        self.mcu.sim_set_safety("estop", False)
        self.assertTrue(wait(lambda: self.mcu.read_status().state == ESTOP, 2))
        self.mcu.send_command(MachineCmd.RESET_ALARM)          # screen reset must not leave E-Stop
        time.sleep(0.1)
        self.assertEqual(self.mcu.read_status().state, ESTOP)


if __name__ == "__main__":
    unittest.main()
