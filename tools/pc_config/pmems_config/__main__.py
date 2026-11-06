"""
Command line tool:  python -m pmems_config --host 192.168.1.200 <command>

  status                          trang thai may
  recipe-list                     liet ke 16 recipe
  recipe-get N                    xem recipe N
  recipe-set N [--diameter MM ...] [--activate]   sua & luu recipe N
  select N                        chon recipe N lam recipe dang dung
  cmd start|stop|pause|reset|home
"""
from __future__ import annotations

import argparse
import sys

from .client import PmemsClient, PmemsError
from .registers import MachineCmd, Recipe

_CMDS = {
    "start": MachineCmd.START, "stop": MachineCmd.STOP, "pause": MachineCmd.PAUSE,
    "reset": MachineCmd.RESET_ALARM, "home": MachineCmd.START_HOMING,
}

_FIELDS = [
    ("name", str, "ten san pham (<=15 ky tu)"),
    ("diameter_mm", float, "duong kinh lon (mm)"),
    ("height_mm", float, "chieu cao lon (mm)"),
    ("tab_offset_deg", float, "goc lech khoen - tam tem (do)"),
    ("m6_height_mm", float, "vi tri truc nang ha M6 (mm)"),
    ("speed_cans_min", int, "toc do (lon/phut)"),
    ("m4_overrun_pulses", int, "xung chay them M4"),
    ("press_ms", int, "thoi gian ep dan (ms)"),
    ("vacuum_detect_ms", int, "tre kiem tra chan khong (ms)"),
    ("blow_off_ms", int, "thoi gian thoi nha tem (ms)"),
]


def _print_recipe(r: Recipe) -> None:
    print(f"Recipe {r.recipe_id}: {r.name}")
    for f, _, label in _FIELDS[1:]:
        print(f"  {label:32s} {getattr(r, f)}")
    print(f"  {'he so M2 (x1000, tu tinh)':32s} {r.m2_scale_x1000}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="pmems_config", description="Cau hinh PMeMS Controller 1 qua Ethernet")
    p.add_argument("--host", default="192.168.1.200")
    p.add_argument("--port", type=int, default=502)
    sub = p.add_subparsers(dest="action", required=True)
    sub.add_parser("status")
    sub.add_parser("recipe-list")
    g = sub.add_parser("recipe-get")
    g.add_argument("id", type=int)
    g.add_argument("--defaults", action="store_true", help="xem gia tri mac dinh nha may")
    s = sub.add_parser("recipe-set")
    s.add_argument("id", type=int)
    for f, typ, label in _FIELDS:
        s.add_argument("--" + f.replace("_", "-"), dest=f, type=typ, help=label)
    s.add_argument("--activate", action="store_true", help="luu va dung ngay recipe nay")
    sel = sub.add_parser("select")
    sel.add_argument("id", type=int)
    c = sub.add_parser("cmd")
    c.add_argument("name", choices=sorted(_CMDS))
    a = p.parse_args(argv)

    try:
        with PmemsClient(a.host, a.port) as plc:
            if a.action == "status":
                st = plc.read_status()
                print(f"Trang thai : {st.state_name}")
                print(f"Alarm      : {st.alarm_name} {st.active_alarms}")
                print(f"Toc do     : {st.speed_cans_min} lon/phut, tren bang tai: {st.cans_on_belt}")
                print(f"San luong  : nap {st.fed}, OK {st.ok}, NG {st.ng}")
                print(f"Recipe     : {plc.active_recipe_id()} (doi duoc: {st.recipe_change_allowed})")
            elif a.action == "recipe-list":
                active = plc.active_recipe_id()
                for r in plc.list_recipes():
                    mark = "*" if r.recipe_id == active else " "
                    print(f"{mark}{r.recipe_id:2d} {r.name:15s} D={r.diameter_mm:5.1f} H={r.height_mm:5.1f} "
                          f"M6={r.m6_height_mm:4.1f} {r.speed_cans_min} lon/ph")
            elif a.action == "recipe-get":
                _print_recipe(plc.read_recipe(a.id, defaults=a.defaults))
            elif a.action == "recipe-set":
                r = plc.read_recipe(a.id)
                for f, _, _ in _FIELDS:
                    v = getattr(a, f)
                    if v is not None:
                        setattr(r, f, v)
                _print_recipe(plc.save_recipe(r, activate=a.activate))
                print("Da luu." + (" Dang dung recipe nay." if a.activate else ""))
            elif a.action == "select":
                plc.select_recipe(a.id)
                print(f"Da chon recipe {a.id}")
            elif a.action == "cmd":
                plc.send_command(_CMDS[a.name])
                print("Da gui lenh", a.name)
    except (PmemsError, ValueError) as e:
        print("LOI:", e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
