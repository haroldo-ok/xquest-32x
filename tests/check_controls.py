#!/usr/bin/env python3
"""
Regression test: pad controllability.

Reproduces the reported problem ("D-pad too sensitive, hard not to
collide against the walls"): taps each direction for ~0.6s, releases,
and waits. With the tuned controls the ship must coast to a stop well
away from the walls and still be alive (an intact ship sprite must be
template-matched on screen). With the old runaway-acceleration
controls the ship kept drifting and died against the wall.
"""
import re
import struct
import subprocess
import sys
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CORE = os.environ.get("CORE", "/home/user/tools/picodrive/picodrive_libretro.so")
ROM = os.environ.get("ROM", os.path.join(ROOT, "build", "xquest32x.32x"))
ART = os.path.join(HERE, "artifacts")

# tap-release in all four directions, generous settle time after each;
# ship starts at page centre, walls are ~185 px away, so a controllable
# ship must never reach them from a 35-frame tap.
SCRIPT = """\
run 420
press START 5
run 30
press B 5
run 5
press RIGHT 35
run 90
shot {art}/ctrl_right.ppm
press LEFT 35
run 90
shot {art}/ctrl_left.ppm
press UP 35
run 90
shot {art}/ctrl_up.ppm
press DOWN 35
run 90
shot {art}/ctrl_down.ppm
"""

SHOTS = ["ctrl_right", "ctrl_left", "ctrl_up", "ctrl_down"]


def load_frames():
    d = open(os.path.join(ROOT, "assets", "xquest.gfx"), "rb").read()
    pos = 0
    frames = []
    while len(frames) < 24:
        w, h = struct.unpack_from("<hh", d, pos)
        bw = ((w - 1) // 4 + 1) * 4
        px = np.frombuffer(d[pos + 4:pos + 4 + bw * h],
                           dtype=np.uint8).reshape(h, bw)
        frames.append(px)
        pos += 4 + bw * h
    return frames


def load_palette():
    pt = re.sub(r"\{[^}]*\}", "",
                open(os.path.join(ROOT, "assets", "palette.inc")).read())
    nums = [int(x) for x in re.findall(r"-?\d+", pt)][4:4 + 768]

    def to_rgb(ci):
        ci = int(ci)
        r, g, b = nums[ci * 3:ci * 3 + 3]
        return tuple((((v * 31 + 31) // 63) << 3) for v in (r, g, b))

    return to_rgb


def ship_alive(ppm, frames, to_rgb):
    """Return (best_diff, frame, x, y) of the best ship-frame match."""
    a = np.array(Image.open(ppm)).astype(np.int32)
    best = (1e9, -1, 0, 0)
    for fi, fr in enumerate(frames):
        h, bw = fr.shape
        target = np.zeros((h, bw, 3), np.int32)
        mask = fr != 0
        for y in range(h):
            for x in range(bw):
                if fr[y, x]:
                    target[y, x] = to_rgb(fr[y, x])
        for yy in range(25, a.shape[0] - h):
            for xx in range(0, a.shape[1] - bw):
                win = a[yy:yy + h, xx:xx + bw, :3]
                diff = np.abs(win - target)[mask].mean()
                if diff < best[0]:
                    best = (diff, fi, xx, yy)
    return best


def main():
    os.makedirs(ART, exist_ok=True)
    script_path = os.path.join(ART, "controls.script")
    with open(script_path, "w") as f:
        f.write(SCRIPT.format(art=ART))

    r = subprocess.run([os.path.join(HERE, "runner"), CORE, ROM, script_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print("FAIL: runner exited nonzero")
        return 1

    frames = load_frames()
    to_rgb = load_palette()
    fail = 0
    for name in SHOTS:
        diff, fi, x, y = ship_alive(os.path.join(ART, name + ".ppm"),
                                    frames, to_rgb)
        # the ship must be intact (good template match) and not pressed
        # against the visible screen edges
        ok = diff < 8.0 and 30 < x < 274 and 40 < y < 185
        print(f"{'PASS' if ok else 'FAIL'}: {name}: ship frame {fi} at "
              f"({x},{y}), match diff {diff:.1f}")
        if not ok:
            fail = 1
    return fail


if __name__ == "__main__":
    sys.exit(main())
