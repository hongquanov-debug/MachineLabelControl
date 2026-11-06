"""
PMeMS VisionControl - PC software for the can orientation & labeling line.

    python main.py --sim          # no controller, no camera: everything simulated
    python main.py                # real controller (Modbus TCP) + RS485_3 + cameras
    python main.py --config other.json --fullscreen
"""
import argparse
import sys

from visioncontrol.app import create


def main() -> int:
    ap = argparse.ArgumentParser(description="PMeMS VisionControl")
    ap.add_argument("--sim", action="store_true", help="giả lập bộ điều khiển và camera")
    ap.add_argument("--config", default="config/settings.json")
    ap.add_argument("--fullscreen", action="store_true", default=None)
    a = ap.parse_args()
    app, _ctrl, _win = create(a.config, sim=a.sim, fullscreen=a.fullscreen)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
