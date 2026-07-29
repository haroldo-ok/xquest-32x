#!/usr/bin/env python3
"""
Regression test: missile (shot) velocity.

Reproduces the reported bug: "if the player shoots from a standing
position, the shots are barely moving". The test aims the ship right
(tap RIGHT, then wait for the auto-coast to bring it to a standstill,
which leaves the facing direction at 'right'), fires one shot, and
takes screenshots every 3 frames. The missile is located by
frame-differencing (ignoring the ship area) and its x position must
advance by at least 5 px/frame.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CORE = os.environ.get("CORE", "/home/user/tools/picodrive/picodrive_libretro.so")
ROM = os.environ.get("ROM", os.path.join(ROOT, "build", "xquest32x.32x"))
ART = os.path.join(HERE, "artifacts")

NSHOTS = 6
STEP = 3

def build_script():
    lines = [
        "run 420",
        "press START 5",
        "run 30",
        "press B 5",
        "run 5",
        # aim right, then let auto-coast stop the ship (facing stays right)
        "press RIGHT 20",
        "run 90",
        f"shot {ART}/mshot_base.ppm",
        "press B 2",
    ]
    for i in range(NSHOTS):
        lines.append(f"run {STEP}")
        lines.append(f"shot {ART}/mshot_{i}.ppm")
    return "\n".join(lines) + "\n"


def diff_blob_x(a, b, exclude=None):
    """rightmost x of changed pixels between frames a and b (playfield
    only), optionally excluding a rectangle (the ship)."""
    d = np.abs(a.astype(int) - b.astype(int)).sum(axis=2)
    d[:30, :] = 0            # status bar
    d[200:, :] = 0
    if exclude:
        x0, y0, x1, y1 = exclude
        d[y0:y1, x0:x1] = 0
    ys, xs = np.where(d > 60)
    if len(xs) == 0:
        return None
    return int(xs.max())


def find_ship(a):
    """rough ship location: cluster of saturated blue cockpit pixels"""
    m = (a[:, :, 2] > 150) & (a[:, :, 0] < 100) & (a[:, :, 1] < 100)
    ys, xs = np.where(m[30:200])
    if len(xs) == 0:
        return 160, 115
    return int(xs.mean()), int(ys.mean()) + 30


def main():
    os.makedirs(ART, exist_ok=True)
    script = os.path.join(ART, "mshot.script")
    with open(script, "w") as f:
        f.write(build_script())

    r = subprocess.run([os.path.join(HERE, "runner"), CORE, ROM, script],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print("FAIL: runner exited nonzero")
        return 1

    base = np.array(Image.open(os.path.join(ART, "mshot_base.ppm")))
    sx, sy = find_ship(base)
    excl = (sx - 14, sy - 14, sx + 14, sy + 14)

    xs = []
    prev = base
    for i in range(NSHOTS):
        cur = np.array(Image.open(os.path.join(ART, f"mshot_{i}.ppm")))
        x = diff_blob_x(prev, cur, exclude=excl)
        xs.append(x)
        prev = cur

    print(f"ship at ({sx},{sy}); missile rightmost-x per step: {xs}")

    # Robust speed estimate: consecutive-step deltas, ignoring outliers
    # (blinking HUD/powerup pixels can pollute a single diff).
    deltas = []
    for i in range(1, len(xs)):
        if xs[i] is not None and xs[i - 1] is not None:
            d = xs[i] - xs[i - 1]
            if -5 < d < 80:          # plausible per-STEP movement
                deltas.append(d)
    if len(deltas) < 2:
        print("FAIL: missile not tracked (did it move at all?)")
        return 1
    deltas.sort()
    med = deltas[len(deltas) // 2]
    speed = med / STEP
    print(f"per-step deltas {deltas}, median {med} -> {speed:.1f} px/frame")
    if speed < 5.0:
        print("FAIL: standing shot too slow")
        return 1
    print("PASS: standing shot is fast")
    return 0


if __name__ == "__main__":
    sys.exit(main())
